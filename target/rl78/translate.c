#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "cpu.h"
#include "tcg/tcg-op.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/translator.h"
#include "exec/translation-block.h"

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef HELPER_H

typedef struct DisasContext {
    DisasContextBase base;
    CPURL78State    *env;
    uint32_t    pc;
    uint32_t    tb_flags;
} DisasContext;

enum {
    DISAS_JUMP = DISAS_TARGET_0,
};

/* register indexes */
static TCGv_i32 cpu_regs[8];
static TCGv_i32 cpu_psw_cy; 
static TCGv_i32 cpu_psw_isp; 
static TCGv_i32 cpu_psw_rbs; 
static TCGv_i32 cpu_psw_ac; 
static TCGv_i32 cpu_psw_z;
static TCGv_i32 cpu_psw_ie;

static TCGv_i32 cpu_pc;
static TCGv_i32 cpu_sp;
static TCGv_i32 cpu_es;
static TCGv_i32 cpu_cs;

static uint64_t decode_load_bytes(DisasContext *ctx, uint64_t insn, 
                                  int i, int n)
{
    for(int idx = i; idx < n; idx++) {
        uint64_t b = translator_ldub(ctx->env, &ctx->base, ctx->base.pc_next++);
        insn |= b << (64 - idx * 8);
    }

    return insn;
}

#include "decode-insn.c.inc"

void rl78_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    CPURL78State *env = cpu_env(cs);
    const uint8_t psw = rl78_cpu_pack_psw(env->psw);

    qemu_fprintf(f, "pc=0x%06x psw=0x%02x\n", env->pc, psw);
    for(int i = 0; i < 8; i++) {
        // TODO: treat bank registers
        qemu_fprintf(f, "r%d=0x%02x\n", i, env->regs[0][i]);
    }
}

static void gen_goto_tb(DisasContext *dc, unsigned tb_slot_idx, vaddr dest)
{
    if (translator_use_goto_tb(&dc->base, dest)) {
        tcg_gen_goto_tb(tb_slot_idx);
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_exit_tb(dc->base.tb, tb_slot_idx);
    } else {
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_lookup_and_goto_ptr();
    }
    dc->base.is_jmp = DISAS_NORETURN;
}

/* generic load wrapper */
/*
static void rl78_gen_lb(DisasContext *ctx, TCGv_i32 dst, TCGv_i32 mem)
{
    tcg_gen_qemu_ld_i32(dst, mem, 0, MO_8 | MO_LE);
}
*/

/*
static void rl78_gen_lw(DisasContext *ctx, TCGv_i32 dst, TCGv_i32 mem)
{
    tcg_gen_qemu_ld_i32(dst, mem, 0, MO_16 | MO_LE);
}
*/

static void rl78_gen_sb(DisasContext *ctx, TCGv_i32 src, TCGv_i32 mem)
{
    tcg_gen_qemu_st_i32(src, mem, 0, MO_8 | MO_LE);
}

/*
static void rl78_gen_sw(DisasContext *ctx, TCGv_i32 src, TCGv_i32 mem)
{
    tcg_gen_qemu_st_i32(src, mem, 0, MO_16 | MO_LE);
}
*/

static bool trans_MOV_ri(DisasContext *ctx, arg_MOV_ri *a) 
{
    tcg_gen_movi_i32(cpu_regs[a->rd], a->imm);
    return true;
}

static bool trans_MOV_saddr_i(DisasContext *ctx, arg_MOV_saddr_i *a)
{
    TCGv_i32 imm, mem;
    imm = tcg_temp_new_i32();
    mem = tcg_temp_new_i32();

    tcg_gen_movi_i32(imm, a->imm);
    tcg_gen_movi_i32(mem, a->saddr + 0xFFE20);
    rl78_gen_sb(ctx, imm, mem);

    return true;
}

static bool trans_MOV_sfr_i(DisasContext *ctx, arg_MOV_sfr_i *a)
{
    TCGv_i32 imm, mem;
    imm = tcg_temp_new_i32();
    mem = tcg_temp_new_i32();

    tcg_gen_movi_i32(imm, a->imm);
    tcg_gen_movi_i32(mem, a->sfr + 0xFFF00);
    rl78_gen_sb(ctx, imm, mem);

    return true;
}

static bool trans_MOV_addr_i(DisasContext *ctx, arg_MOV_addr_i *a)
{
    TCGv_i32 imm, mem;
    imm = tcg_temp_new_i32();
    mem = tcg_temp_new_i32();

    tcg_gen_movi_i32(imm, a->imm);
    tcg_gen_movi_i32(mem, a->addr + 0xF0000);
    rl78_gen_sb(ctx, imm, mem);

    return true;
}

static bool trans_MOV_PSW_A(DisasContext *ctx, arg_MOV_PSW_A *a) 
{
    TCGv_i32 psw_cy, psw_isp, psw_rbs0, psw_rbs1, psw_ac, psw_z, psw_ie;
    psw_cy = tcg_temp_new_i32();
    psw_isp = tcg_temp_new_i32();
    psw_rbs0 = tcg_temp_new_i32();
    psw_rbs1 = tcg_temp_new_i32();
    psw_ac = tcg_temp_new_i32();
    psw_z = tcg_temp_new_i32();
    psw_ie = tcg_temp_new_i32();

    tcg_gen_andi_i32(psw_cy, cpu_regs[1], 0x01);
    tcg_gen_andi_i32(psw_isp, cpu_regs[1], 0x06);
    tcg_gen_andi_i32(psw_rbs0, cpu_regs[1], 0x08);
    tcg_gen_andi_i32(psw_rbs1, cpu_regs[1], 0x20);
    tcg_gen_andi_i32(psw_ac, cpu_regs[1], 0x10);
    tcg_gen_andi_i32(psw_z, cpu_regs[1], 0x40);
    tcg_gen_andi_i32(psw_ie, cpu_regs[1], 0x80);

    tcg_gen_shri_i32(psw_isp, psw_isp, 1);
    tcg_gen_shri_i32(psw_rbs0, psw_rbs0, 3);
    tcg_gen_shri_i32(psw_rbs1, psw_rbs1, 4);
    tcg_gen_shri_i32(psw_ac, psw_ac, 4);
    tcg_gen_shri_i32(psw_z, psw_z, 6);
    tcg_gen_shri_i32(psw_ie, psw_ie, 7);

    tcg_gen_or_i32(psw_rbs0, psw_rbs0, psw_rbs1);

    tcg_gen_mov_i32(cpu_psw_cy, psw_cy);
    tcg_gen_mov_i32(cpu_psw_isp, psw_isp);
    tcg_gen_mov_i32(cpu_psw_rbs, psw_rbs0);
    tcg_gen_mov_i32(cpu_psw_ac, psw_ac);
    tcg_gen_mov_i32(cpu_psw_z, psw_z);
    tcg_gen_mov_i32(cpu_psw_ie, psw_ie);

    return true;
}

static bool trans_MOV_A_PSW(DisasContext *ctx, arg_MOV_A_PSW *a)
{
    TCGv_i32 psw_cy, psw_isp, psw_rbs0, psw_rbs1, psw_ac, psw_z, psw_ie;
    psw_cy = tcg_temp_new_i32();
    psw_isp = tcg_temp_new_i32();
    psw_rbs0 = tcg_temp_new_i32();
    psw_ac = tcg_temp_new_i32();
    psw_rbs1 = tcg_temp_new_i32();
    psw_z = tcg_temp_new_i32();
    psw_ie = tcg_temp_new_i32();


    tcg_gen_mov_i32(psw_cy, cpu_psw_cy);
    tcg_gen_shli_i32(psw_isp, cpu_psw_isp, 1);
    tcg_gen_andi_i32(psw_rbs0, cpu_psw_rbs, 1);
    tcg_gen_shli_i32(psw_rbs0, psw_rbs0, 3);
    tcg_gen_shli_i32(psw_ac, cpu_psw_ac, 4);
    tcg_gen_andi_i32(psw_rbs1, cpu_psw_rbs, 2);
    tcg_gen_shli_i32(psw_rbs1, psw_rbs0, 5);
    tcg_gen_shli_i32(psw_z, cpu_psw_z, 6);
    tcg_gen_shli_i32(psw_ie, cpu_psw_ie, 7);

    tcg_gen_mov_i32(cpu_regs[1], psw_cy);
    tcg_gen_or_i32(cpu_regs[1], cpu_regs[1], psw_isp);
    tcg_gen_or_i32(cpu_regs[1], cpu_regs[1], psw_rbs0);
    tcg_gen_or_i32(cpu_regs[1], cpu_regs[1], psw_ac);
    tcg_gen_or_i32(cpu_regs[1], cpu_regs[1], psw_rbs1);
    tcg_gen_or_i32(cpu_regs[1], cpu_regs[1], psw_z);
    tcg_gen_or_i32(cpu_regs[1], cpu_regs[1], psw_ie);

    return true;
}

static bool trans_BR_addr16(DisasContext *ctx, arg_BR_addr16 *a)
{
    gen_goto_tb(ctx, 0, a->addr);
    return true;
}

static bool trans_BNZ(DisasContext *ctx, arg_BNZ *a)
{   
    const vaddr br_pc = ctx->base.pc_next + (int8_t)a->addr;
    TCGLabel *target = gen_new_label();

    tcg_gen_brcondi_i32(TCG_COND_NE, cpu_psw_z, 0, target);
    gen_goto_tb(ctx, 0, br_pc);
    gen_set_label(target);
    
    return true;
}


static void rl78_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    ctx->env = cpu_env(cs);
    ctx->tb_flags = ctx->base.tb->flags;
}

static void rl78_tr_tb_start(DisasContextBase *dcbase, CPUState *cs)
{
}

static void rl78_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_next);
}

static void rl78_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    uint64_t insn;

    ctx->pc = ctx->base.pc_next;
    insn = decode_load(ctx);
    if(!decode(ctx, insn)) {
        // TODO: gen_helper_raise_illegal_instruction(tcg_env);
    }
}

static void rl78_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_NEXT:
    case DISAS_TOO_MANY:
        gen_goto_tb(ctx, 0, dcbase->pc_next);
        break;
    case DISAS_JUMP:
        tcg_gen_lookup_and_goto_ptr();
        break;
    case DISAS_NORETURN:
        break;
    default:
        g_assert_not_reached();
    }
}

static const TranslatorOps rl78_tr_ops = {
    .init_disas_context = rl78_tr_init_disas_context,
    .tb_start           = rl78_tr_tb_start,
    .insn_start         = rl78_tr_insn_start,
    .translate_insn     = rl78_tr_translate_insn,
    .tb_stop            = rl78_tr_tb_stop,
};

void rl78_translate_code(CPUState *cs, TranslationBlock *tb, 
                         int *max_insns, vaddr pc, void *host_pc)
{
    DisasContext dc;

    translator_loop(cs, tb, max_insns, pc, host_pc, &rl78_tr_ops, &dc.base);
}

#define ALLOC_REGISTER(sym, member, name) \
    cpu_##sym = tcg_global_mem_new_i32(tcg_env, \
                                       offsetof(CPURL78State, member), name)

void rl78_translate_init(void)
{
    static const char* const regnames[GPREG_NUM] = {
        "R0", "R1", "R2", "R3",
        "R4", "R5", "R6", "R7",
    };

    for(int i = 0; i < GPREG_NUM; i++) {
        cpu_regs[i] = tcg_global_mem_new_i32(tcg_env, 
                                             offsetof(CPURL78State, regs[i]),
                                             regnames[i]);
    }

    ALLOC_REGISTER(pc, pc, "PC");
    ALLOC_REGISTER(psw_cy, psw.cy, "PSW(CY)");
    ALLOC_REGISTER(psw_isp, psw.isp, "PSW(ISP)");
    ALLOC_REGISTER(psw_rbs, psw.rbs, "PSW(RBS)");
    ALLOC_REGISTER(psw_ac, psw.ac, "PSW(AC)");
    ALLOC_REGISTER(psw_z, psw.z, "PSW(Z)");
    ALLOC_REGISTER(psw_ie, psw.ie, "PSW(IE)");

    ALLOC_REGISTER(sp, sp, "SP");
    ALLOC_REGISTER(es, es, "ES");
    ALLOC_REGISTER(cs, cs, "CS");
}