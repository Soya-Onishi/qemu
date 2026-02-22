#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "cpu.h"
#include "tcg/tcg-op-common.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-cond.h"
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
    bool use_es;
} DisasContext;

enum {
    DISAS_LOOKUP = DISAS_TARGET_0,
    DISAS_EXIT = DISAS_TARGET_1,
};

#define TB_EXIT_JUMP TB_EXIT_IDX1
#define TB_EXIT_NOJUMP TB_EXIT_IDX0

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

static TCGv_i32 cpu_skip_en;
static TCGv_i32 cpu_skip_req;

static uint64_t decode_load_bytes(DisasContext *ctx, uint64_t insn, 
                                  int i, int n)
{
    const int cnt = n - i;
    for(int idx = 0; idx < cnt; idx++) {
        const uint64_t shamt = 64 - (i + idx + 1) * 8;
        uint64_t b = translator_ldub(ctx->env, &ctx->base, 
                                      ctx->base.pc_next + idx);

        if(b == 0x11 && i == 0 && idx == 0) {
            ctx->use_es = true;
            ctx->base.pc_next += 1;
            idx--;
            continue;
        }

        insn |= b << shamt;
    }

    ctx->base.pc_next += cnt;

    return insn;
}

#include "decode-insn.c.inc"

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

void rl78_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    CPURL78State *env = cpu_env(cs);

    qemu_fprintf(f, "pc=0x%06x\n", env->pc);
    qemu_fprintf(f, "sp=0xf%04x\n", env->sp);

    qemu_fprintf(f, "psw.cy=0x%02x\n", env->psw.cy);
    qemu_fprintf(f, "psw.isp=0x%02x\n", env->psw.isp);
    qemu_fprintf(f, "psw.rbs=0x%02x\n", env->psw.rbs);
    qemu_fprintf(f, "psw.ac=0x%02x\n", env->psw.ac);
    qemu_fprintf(f, "psw.z=0x%02x\n", env->psw.z);
    qemu_fprintf(f, "psw.ie=0x%02x\n", env->psw.ie);

    qemu_fprintf(f, "es=0x%02x\n", env->es);
    qemu_fprintf(f, "cs=0x%02x\n", env->cs);
}

static bool trans_NOP(DisasContext *ctx, arg_NOP *a)
{
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

    tcg_gen_insn_start(ctx->base.pc_next, 0, 0);
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

        tcg_gen_movi_i32(cpu_skip_en, 0);
        tcg_gen_mov_i32(required_tmp, cpu_skip_req);
        tcg_gen_movi_i32(cpu_skip_req, 0);

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
        tcg_gen_exit_tb(NULL, TB_EXIT_NOJUMP);
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
    dc.skip_flag = env->skip_en;

    translator_loop(cs, tb, max_insns, pc, host_pc, &rl78_tr_ops, &dc.base);
}

#define ALLOC_REGISTER(sym, member, name) \
    cpu_##sym = tcg_global_mem_new_i32(tcg_env, \
                                       offsetof(CPURL78State, member), name)

void rl78_translate_init(void)
{
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

    ALLOC_REGISTER(skip_en, skip_en, "SKIP(EN)");
    ALLOC_REGISTER(skip_req, skip_req, "SKIP(REQ)");
}
