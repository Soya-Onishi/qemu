#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "cpu.h"
#include "tcg/tcg-op-common.h"
#include "tcg/tcg-op.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/translator.h"
#include "exec/translation-block.h"
#include "tcg/tcg.h"

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef HELPER_H

typedef struct DisasContext {
    DisasContextBase base;
    CPURL78State    *env;
    uint32_t    pc;
    uint32_t    tb_flags;

    bool skip_flag;
} DisasContext;

enum {
    DISAS_LOOKUP = DISAS_TARGET_0,
    DISAS_EXIT = DISAS_TARGET_1,
};

#define TB_EXIT_JUMP TB_EXIT_IDX1
#define TB_EXIT_NOJUMP TB_EXIT_IDX0

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

static TCGv_i32 cpu_skip_enabled;
static TCGv_i32 cpu_skip_required;

static uint64_t decode_load_bytes(DisasContext *ctx, uint64_t insn, 
                                  int i, int n)
{
    const int cnt = n - i;
    for(int idx = 0; idx < cnt; idx++) {
        const uint64_t shamt = 64 - (i + idx + 1) * 8;
        const uint64_t b = translator_ldub(ctx->env, &ctx->base, 
                                           ctx->base.pc_next + idx);

        insn |= b << shamt;
    }

    ctx->base.pc_next += cnt;

    return insn;
}

static inline uint32_t rl78_word(uint32_t v) 
{
    return ((v & 0x0000FF) << 8) | ((v & 0x00FF00) >> 8);
}

#include "decode-insn.c.inc"

void rl78_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    CPURL78State *env = cpu_env(cs);
    const uint8_t psw = rl78_cpu_pack_psw(env->psw);

    qemu_fprintf(f, "pc=0x%06x\n", env->pc);
    qemu_fprintf(f, "psw=0x%02x\n", psw);
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

static void gen_exit_tb(DisasContext *dc, const vaddr pc_next) {
    tcg_gen_movi_i32(cpu_pc, pc_next);
    tcg_gen_exit_tb(NULL, TB_EXIT_IDX0);
    dc->base.is_jmp = DISAS_EXIT;
}

/* generic load wrapper */
static void rl78_gen_lb(DisasContext *ctx, TCGv_i32 dst, TCGv ptr)
{
    tcg_gen_qemu_ld_i32(dst, ptr, 0, MO_8);
}


/*
static void rl78_gen_lw(DisasContext *ctx, TCGv_i32 dst, TCGv_i32 mem)
{
    tcg_gen_qemu_ld_i32(dst, mem, 0, MO_16 | MO_LE);
}
*/

static void rl78_gen_sb(DisasContext *ctx, TCGv_i32 src, TCGv ptr)
{
    tcg_gen_qemu_st_i32(src, ptr, 0, MO_8);
}

/*
static void rl78_gen_sw(DisasContext *ctx, TCGv_i32 src, TCGv_i32 mem)
{
    tcg_gen_qemu_st_i32(src, mem, 0, MO_16 | MO_LE);
}
*/

static void rl78_store_psw(DisasContext *ctx, TCGv_i32 src) 
{
    TCGv_i32 psw_cy, psw_isp, psw_rbs0, psw_rbs1, psw_ac, psw_z, psw_ie;
    psw_cy = tcg_temp_new_i32();
    psw_isp = tcg_temp_new_i32();
    psw_rbs0 = tcg_temp_new_i32();
    psw_rbs1 = tcg_temp_new_i32();
    psw_ac = tcg_temp_new_i32();
    psw_z = tcg_temp_new_i32();
    psw_ie = tcg_temp_new_i32();

    tcg_gen_andi_i32(psw_cy,   src, 0x01);
    tcg_gen_andi_i32(psw_isp,  src, 0x06);
    tcg_gen_andi_i32(psw_rbs0, src, 0x08);
    tcg_gen_andi_i32(psw_rbs1, src, 0x20);
    tcg_gen_andi_i32(psw_ac,   src, 0x10);
    tcg_gen_andi_i32(psw_z,    src, 0x40);
    tcg_gen_andi_i32(psw_ie,   src, 0x80);

    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_psw_cy, psw_cy, 0);

    tcg_gen_shri_i32(psw_isp, psw_isp, 1); 
    tcg_gen_mov_i32(cpu_psw_isp, psw_isp);

    tcg_gen_shri_i32(psw_rbs0, psw_rbs0, 3);
    tcg_gen_shri_i32(psw_rbs1, psw_rbs1, 4);
    tcg_gen_or_i32(cpu_psw_rbs, psw_rbs0, psw_rbs1);

    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_psw_ac, psw_ac, 0);
    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_psw_z,  psw_z,  0);
    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_psw_ie, psw_ie, 0);

    gen_exit_tb(ctx, ctx->base.pc_next);
}

static bool MOV_A_rs(RL78GPRegister rs) 
{
    tcg_gen_mov_i32(cpu_regs[RL78_GPREG_A], cpu_regs[rs]);
    return true;
}

static bool MOV_rd_A(RL78GPRegister rd) 
{
    tcg_gen_mov_i32(cpu_regs[rd], cpu_regs[RL78_GPREG_A]);
    return true;
}

static bool trans_MOV_ri(DisasContext *ctx, arg_MOV_ri *a) 
{
    tcg_gen_movi_i32(cpu_regs[a->rd], a->imm);
    return true;
}

static bool trans_MOV_PSW_i(DisasContext *ctx, arg_MOV_PSW_i *a)
{
    TCGv_i32 src = tcg_constant_i32(a->imm);
    rl78_store_psw(ctx, src);
    return true;
}

static bool trans_MOV_A_X(DisasContext *ctx, arg_MOV_A_X *a) 
{
    return MOV_A_rs(RL78_GPREG_X);
}

static bool trans_MOV_A_C(DisasContext *ctx, arg_MOV_A_C *a) 
{
    return MOV_A_rs(RL78_GPREG_C);
}

static bool trans_MOV_A_B(DisasContext *ctx, arg_MOV_A_B *a) 
{
    return MOV_A_rs(RL78_GPREG_B);
}

static bool trans_MOV_A_E(DisasContext *ctx, arg_MOV_A_E *a) 
{
    return MOV_A_rs(RL78_GPREG_E);
}

static bool trans_MOV_A_D(DisasContext *ctx, arg_MOV_A_D *a) 
{
    return MOV_A_rs(RL78_GPREG_D);
}

static bool trans_MOV_A_L(DisasContext *ctx, arg_MOV_A_L *a) 
{
    return MOV_A_rs(RL78_GPREG_L);
}

static bool trans_MOV_A_H(DisasContext *ctx, arg_MOV_A_H *a) 
{
    return MOV_A_rs(RL78_GPREG_H);
}

static bool trans_MOV_X_A(DisasContext *ctx, arg_MOV_X_A *a) 
{
    return MOV_rd_A(RL78_GPREG_X);
}

static bool trans_MOV_C_A(DisasContext *ctx, arg_MOV_X_A *a) 
{
    return MOV_rd_A(RL78_GPREG_C);
}

static bool trans_MOV_B_A(DisasContext *ctx, arg_MOV_B_A *a) 
{
    return MOV_rd_A(RL78_GPREG_B);
}

static bool trans_MOV_E_A(DisasContext *ctx, arg_MOV_E_A *a) 
{
    return MOV_rd_A(RL78_GPREG_E);
}

static bool trans_MOV_D_A(DisasContext *ctx, arg_MOV_D_A *a) 
{
    return MOV_rd_A(RL78_GPREG_D);
}

static bool trans_MOV_L_A(DisasContext *ctx, arg_MOV_L_A *a) 
{
    return MOV_rd_A(RL78_GPREG_L);
}

static bool trans_MOV_H_A(DisasContext *ctx, arg_MOV_H_A *a) 
{
    return MOV_rd_A(RL78_GPREG_H);
}

static bool trans_MOV_saddr_i(DisasContext *ctx, arg_MOV_saddr_i *a)
{
    const uint32_t offset = a->saddr;
    TCGv_i32 imm = tcg_constant_i32(a->imm);
    TCGv ptr = tcg_constant_tl(0xFFE20 + offset);

    rl78_gen_sb(ctx, imm, ptr);

    return true;
}

static bool trans_MOV_sfr_i(DisasContext *ctx, arg_MOV_sfr_i *a)
{
    const uint32_t addr = a->sfr; 
    TCGv_i32 imm = tcg_constant_i32(a->imm);
    TCGv ptr = tcg_constant_tl(0xFFF00 | addr);

    rl78_gen_sb(ctx, imm, ptr);

    return true;
}

static bool trans_MOV_addr_i(DisasContext *ctx, arg_MOV_addr_i *a)
{
    const uint32_t addr = rl78_word(a->addr);
    TCGv_i32 imm = tcg_constant_i32(a->imm);
    TCGv ptr = tcg_constant_tl(0xF0000 | addr);

    rl78_gen_sb(ctx, imm, ptr);

    return true;
}

static bool trans_MOV_addr_r(DisasContext *ctx, arg_MOV_addr_r *a)
{
    const uint32_t addr = rl78_word(a->addr);
    TCGv ptr = tcg_constant_tl(0xF0000 | addr);

    rl78_gen_sb(ctx, cpu_regs[1], ptr);

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

    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_psw_cy, psw_cy, 0);

    tcg_gen_shri_i32(psw_isp, psw_isp, 1); 
    tcg_gen_mov_i32(cpu_psw_isp, psw_isp);

    tcg_gen_shri_i32(psw_rbs0, psw_rbs0, 3);
    tcg_gen_shri_i32(psw_rbs1, psw_rbs1, 4);
    tcg_gen_or_i32(cpu_psw_rbs, psw_rbs0, psw_rbs1);

    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_psw_ac, psw_ac, 0);
    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_psw_z,  psw_z,  0);
    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_psw_ie, psw_ie, 0);

    gen_exit_tb(ctx, ctx->base.pc_next);

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
    tcg_gen_shli_i32(psw_rbs1, psw_rbs1, 4);
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

static bool trans_MOV_A_addr(DisasContext *ctx, arg_MOV_A_addr *a)
{
    const uint32_t addr = rl78_word(a->addr);
    TCGv ptr;

    ptr = tcg_constant_tl(0xF0000 | addr);
    rl78_gen_lb(ctx, cpu_regs[1], ptr);

    return true;
}

static bool trans_CMP_A_i(DisasContext *ctx, arg_CMP_A_i *a)
{
    TCGv_i32 half_acc = tcg_temp_new_i32();

    tcg_gen_andi_i32(half_acc, cpu_regs[1], 0x0F);

    tcg_gen_setcondi_i32(TCG_COND_LTU, cpu_psw_cy, cpu_regs[1], a->imm);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_z, cpu_regs[1], a->imm);
    tcg_gen_setcondi_i32(TCG_COND_LTU, cpu_psw_ac, half_acc, a->imm & 0x0F);

    return true;
}

static bool trans_BR_addr16(DisasContext *ctx, arg_BR_addr16 *a)
{
    const uint32_t addr = rl78_word(a->addr);
    gen_goto_tb(ctx, TB_EXIT_JUMP, addr);
    return true;
}

static bool trans_BNZ(DisasContext *ctx, arg_BNZ *a)
{   
    const vaddr br_pc = ctx->base.pc_next + (int8_t)a->addr;
    TCGLabel *target = gen_new_label();

    tcg_gen_brcondi_i32(TCG_COND_NE, cpu_psw_z, 0, target);
    gen_goto_tb(ctx, TB_EXIT_JUMP, br_pc);
    gen_set_label(target);
    
    return true;
}

static bool trans_SKZ(DisasContext *ctx, arg_SKZ *a)
{
    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_skip_required, cpu_psw_z, 0);
    tcg_gen_movi_i32(cpu_skip_enabled, 1);
    ctx->skip_flag = true;

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
    volatile uint64_t insn;
    TCGLabel* skip_label = NULL;

    const bool use_skip = ctx->skip_flag;
    ctx->skip_flag = false;
    if(use_skip) {
        TCGv_i32 required_tmp = tcg_temp_new_i32();

        tcg_gen_movi_i32(cpu_skip_enabled, 0);
        tcg_gen_mov_i32(required_tmp, cpu_skip_required);
        tcg_gen_movi_i32(cpu_skip_required, 0);

        skip_label = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_NE, required_tmp, 0, skip_label);
    }

    ctx->pc = ctx->base.pc_next;
    insn = decode_load(ctx);
    if(!decode(ctx, insn)) {
        exit(1);
        // TODO: gen_helper_raise_illegal_instruction(tcg_env);
    }

    if(use_skip) {
        gen_set_label(skip_label);
    }
}

static void rl78_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_NEXT:
    case DISAS_EXIT:
        break;
    case DISAS_NORETURN:
        gen_goto_tb(ctx, TB_EXIT_NOJUMP, ctx->base.pc_next);
        break;
    case DISAS_LOOKUP:
        tcg_gen_lookup_and_goto_ptr(); 
        tcg_gen_exit_tb(dcbase->tb, TB_EXIT_NOJUMP);
        break;
    case DISAS_TOO_MANY:
        gen_goto_tb(ctx, TB_EXIT_NOJUMP, ctx->base.pc_next);
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
    CPUArchState *env = cpu_env(cs);

    dc.env = env;
    dc.pc = pc;
    dc.tb_flags = tb->flags;
    dc.skip_flag = env->skip_enabled;

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
                                             offsetof(CPURL78State, regs[0][i]),
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

    ALLOC_REGISTER(skip_enabled, skip_enabled, "SKIP(EN)");
    ALLOC_REGISTER(skip_required, skip_required, "SKIP(REQ)");
}