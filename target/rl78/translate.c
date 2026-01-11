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


#define rl78_immw(arg) (tcg_constant_i32((arg)->datal | ((arg)->datah << 8)))

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
    qemu_fprintf(f, "es=0x%02x\n", env->es);
    qemu_fprintf(f, "cs=0x%02x\n", env->cs);
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

static TCGv rl78_gen_addr(const uint32_t adrl, const uint32_t adrh, TCGv_i32 es)
{
    const uint32_t addr16 = (adrl | (adrh << 8)) & (0xFFFF);
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_shli_i32(addr, es, 16);
    tcg_gen_ori_i32(addr, addr, addr16);
    return addr;
}

/* generic load wrapper */
static void rl78_gen_lb(DisasContext *ctx, TCGv_i32 dst, TCGv ptr)
{
    tcg_gen_qemu_ld_i32(dst, ptr, 0, MO_8);
}


static void rl78_gen_lw(DisasContext *ctx, TCGv_i32 dst, TCGv ptr)
{
    TCGv access_ptr = tcg_temp_new_i32();
    tcg_gen_andi_i32(access_ptr, ptr, 0xFFFFE);
    tcg_gen_qemu_ld_i32(dst, access_ptr, 0, MO_16 | MO_LE);
}

static void rl78_gen_sb(DisasContext *ctx, TCGv_i32 src, TCGv ptr)
{
    tcg_gen_qemu_st_i32(src, ptr, 0, MO_8);
}

static void rl78_gen_sw(DisasContext *ctx, TCGv_i32 src, TCGv ptr)
{
    TCGv access_ptr = tcg_temp_new_i32();
    tcg_gen_andi_i32(access_ptr, ptr, 0xFFFFE);
    tcg_gen_qemu_st_i32(src, access_ptr, 0, MO_16 | MO_LE);
}

static TCGv_i32 rl78_load_rp(RL78GPRegister rp)
{
    TCGv_i32 value = tcg_temp_new_i32();

    tcg_gen_mov_i32(value, cpu_regs[rp + 1]);
    tcg_gen_shli_i32(value, value, 8);
    tcg_gen_or_i32(value, value, cpu_regs[rp]);

    return value;
}

static void rl78_store_rp(RL78GPRegister rp, TCGv_i32 value) 
{
    TCGv_i32 temp = tcg_temp_new_i32();

    tcg_gen_andi_i32(cpu_regs[rp], value, 0xFF);
    tcg_gen_andi_i32(temp, value, 0xFF00);
    tcg_gen_shri_i32(cpu_regs[rp+1], temp, 8);
}

static TCGv rl78_gen_saddr(TCGv saddr)
{
    TCGv ret = tcg_temp_new_i32();
    TCGv base = tcg_temp_new_i32();

    tcg_gen_movcond_i32(TCG_COND_LTU, base, 
                        saddr, tcg_constant_i32(0x20),
                        tcg_constant_i32(0xFFF00),
                        tcg_constant_i32(0xFFE00));
    tcg_gen_add_tl(ret, saddr, base);

    return ret;
}

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

static bool trans_MOV_A_saddr(DisasContext *ctx, arg_MOV_A_saddr *a)
{
    TCGv saddr = tcg_constant_tl(a->saddr);
    TCGv ptr = rl78_gen_saddr(saddr);

    rl78_gen_lb(ctx, cpu_regs[1], ptr);

    return true;
}

static bool trans_MOV_saddr_A(DisasContext *ctx, arg_MOV_saddr_A *a)
{
    TCGv saddr = tcg_constant_tl(a->saddr);
    TCGv ptr = rl78_gen_saddr(saddr);

    rl78_gen_sb(ctx, cpu_regs[1], ptr);

    return true;
}

static void rl78_store_es(TCGv_i32 es)
{
    tcg_gen_andi_i32(cpu_es, es, 0x0F);
}

static bool trans_MOV_ES_i(DisasContext *ctx, arg_MOV_ES_i *a)
{
    TCGv_i32 es = tcg_constant_i32(a->imm);
    rl78_store_es(es);

    return true;
}

static bool trans_MOV_ES_saddr(DisasContext *ctx, arg_MOV_ES_saddr *a)
{
    TCGv_i32 es = tcg_temp_new_i32();
    TCGv ptr = rl78_gen_saddr(tcg_constant_tl(a->saddr));
    rl78_gen_lb(ctx, es, ptr);
    rl78_store_es(es);
    
    return true;
}

static bool trans_MOV_ES_A(DisasContext *ctx, arg_MOV_ES_A *a)
{
    TCGv_i32 es = tcg_temp_new_i32();
    tcg_gen_mov_i32(es, cpu_regs[RL78_GPREG_A]);
    rl78_store_es(es);

    return true;
}

static bool trans_MOV_A_ES(DisasContext *ctx, arg_MOV_A_ES *a)
{
    tcg_gen_mov_i32(cpu_regs[RL78_GPREG_A], cpu_es);
    return true;
}

static void rl78_store_cs(TCGv_i32 cs)
{
    tcg_gen_andi_i32(cpu_cs, cs, 0x0F);
}

static bool trans_MOV_CS_i(DisasContext *ctx, arg_MOV_CS_i *a)
{
    rl78_store_cs(tcg_constant_i32(a->imm));
    return true;
}

static bool trans_MOC_A_CS(DisasContext *ctx, arg_MOC_A_CS *a)
{
    tcg_gen_mov_i32(cpu_regs[RL78_GPREG_A], cpu_cs);
    return true;
}

static bool trans_MOV_CS_A(DisasContext *ctx, arg_MOV_CS_A *a)
{
    rl78_store_cs(cpu_regs[RL78_GPREG_A]);
    return true;
}

static TCGv rl78_indirect_ptr(TCGv_i32 base, TCGv_i32 offset)
{
    TCGv ptr = tcg_temp_new_i32();

    tcg_gen_add_i32(ptr, base, offset);
    tcg_gen_ori_i32(ptr, ptr, 0xF0000);

    return ptr;
}

static bool trans_MOV_A_indDE(DisasContext *ctx, arg_MOV_A_indDE *a)
{
    TCGv_i32 de = rl78_load_rp(RL78_GPREG_DE);

    tcg_gen_ori_i32(de, de, 0xF0000);
    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], de);
    
    return true;
}

static bool trans_MOV_indDE_A(DisasContext *ctx, arg_MOV_indDE_A *a) 
{
    TCGv_i32 de = rl78_load_rp(RL78_GPREG_DE);

    tcg_gen_ori_i32(de, de, 0xF0000);
    rl78_gen_sb(ctx, cpu_regs[RL78_GPREG_A], de);
    
    return true;
}

static bool trans_MOV_indDEoffset_i(DisasContext *ctx, arg_MOV_indDEoffset_i *a)
{
    TCGv_i32 imm = tcg_constant_i32(a->imm);
    TCGv ptr = rl78_indirect_ptr(rl78_load_rp(RL78_GPREG_DE), tcg_constant_i32(a->offset));

    rl78_gen_sb(ctx, imm, ptr);

    return true;
}

static bool trans_MOV_A_indDEoffset(DisasContext *ctx, arg_MOV_A_indDEoffset *a)
{
    TCGv ptr = rl78_indirect_ptr(rl78_load_rp(RL78_GPREG_DE), tcg_constant_i32(a->offset));
    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], ptr);

    return true;
}

static bool trans_MOV_indDEoffset_A(DisasContext *ctx, arg_MOV_indDEoffset_A *a)
{
    TCGv ptr = rl78_indirect_ptr(rl78_load_rp(RL78_GPREG_DE), tcg_constant_i32(a->offset));
    rl78_gen_sb(ctx, cpu_regs[RL78_GPREG_A], ptr);
    return true;
}

static bool trans_MOV_A_indHL(DisasContext *ctx, arg_MOV_A_indHL *a)
{
    TCGv ptr = rl78_indirect_ptr(rl78_load_rp(RL78_GPREG_HL), tcg_constant_i32(0));
    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], ptr);
    return true;
}

static bool trans_MOV_indHL_A(DisasContext *ctx, arg_MOV_indHL_A *a)
{
    TCGv ptr = rl78_indirect_ptr(rl78_load_rp(RL78_GPREG_HL), tcg_constant_i32(0));
    rl78_gen_sb(ctx, cpu_regs[RL78_GPREG_A], ptr);
    return true;
}

static bool trans_MOV_indHLoffset_i(DisasContext *ctx, arg_MOV_indHLoffset_i *a)
{
    TCGv ptr = rl78_indirect_ptr(rl78_load_rp(RL78_GPREG_HL), tcg_constant_i32(a->offset));
    rl78_gen_sb(ctx, tcg_constant_i32(a->imm), ptr);
    return true;
}

static bool trans_MOV_A_indHLoffset(DisasContext *ctx, arg_MOV_A_indHLoffset *a)
{
    TCGv ptr = rl78_indirect_ptr(rl78_load_rp(RL78_GPREG_HL), tcg_constant_i32(a->offset));
    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], ptr);
    return true;
}

static bool trans_MOV_indHLoffset_A(DisasContext *ctx, arg_MOV_indHLoffset_A *a)
{
    TCGv ptr = rl78_indirect_ptr(rl78_load_rp(RL78_GPREG_HL), tcg_constant_i32(a->offset));
    rl78_gen_sb(ctx, cpu_regs[RL78_GPREG_A], ptr);
    return true;
}

static bool trans_MOV_A_indHL_B(DisasContext *ctx, arg_MOV_A_indHL_B *a)
{
    TCGv ptr = rl78_indirect_ptr(rl78_load_rp(RL78_GPREG_HL), cpu_regs[RL78_GPREG_B]);
    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], ptr);
    return true;
}

static bool trans_MOV_indHL_B_A(DisasContext *ctx, arg_MOV_indHL_B_A *a)
{
    TCGv ptr = rl78_indirect_ptr(rl78_load_rp(RL78_GPREG_HL), cpu_regs[RL78_GPREG_B]);
    rl78_gen_sb(ctx, cpu_regs[RL78_GPREG_A], ptr);
    return true;
}

static bool trans_MOV_A_indHL_C(DisasContext *ctx, arg_MOV_A_indHL_C *a)
{
    TCGv ptr = rl78_indirect_ptr(rl78_load_rp(RL78_GPREG_HL), cpu_regs[RL78_GPREG_C]);
    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], ptr);
    return true;
}

static bool trans_MOV_indHL_C_A(DisasContext *ctx, arg_MOV_indHL_C_A *a)
{
    TCGv ptr = rl78_indirect_ptr(rl78_load_rp(RL78_GPREG_HL), cpu_regs[RL78_GPREG_C]);
    rl78_gen_sb(ctx, cpu_regs[RL78_GPREG_A], ptr);
    return true;
}

static bool trans_MOV_indBbase_i(DisasContext *ctx, arg_MOV_indBbase_i *a)
{
    TCGv_i32 base = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv ptr = rl78_indirect_ptr(base, cpu_regs[RL78_GPREG_B]);

    rl78_gen_sb(ctx, tcg_constant_i32(a->imm), ptr);

    return true;
}

static bool trans_MOV_A_indBbase(DisasContext *ctx, arg_MOV_A_indBbase *a)
{
    TCGv_i32 base = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv ptr = rl78_indirect_ptr(base, cpu_regs[RL78_GPREG_B]);

    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], ptr);

    return true;
}

static bool trans_MOV_indBbase_A(DisasContext *ctx, arg_MOV_indBbase_A *a)
{
    TCGv_i32 base = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv ptr = rl78_indirect_ptr(base, cpu_regs[RL78_GPREG_B]);

    rl78_gen_sb(ctx, cpu_regs[RL78_GPREG_A], ptr);

    return true;
}

static bool trans_MOV_indCbase_i(DisasContext *ctx, arg_MOV_indCbase_i *a)
{
    TCGv_i32 base = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv ptr = rl78_indirect_ptr(base, cpu_regs[RL78_GPREG_C]);

    rl78_gen_sb(ctx, tcg_constant_i32(a->imm), ptr);

    return true;
}

static bool trans_MOV_A_indCbase(DisasContext *ctx, arg_MOV_A_indCbase *a)
{
    TCGv_i32 base = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv ptr = rl78_indirect_ptr(base, cpu_regs[RL78_GPREG_C]);

    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], ptr);

    return true;
}

static bool trans_MOV_indCbase_A(DisasContext *ctx, arg_MOV_indCbase_A *a)
{
    TCGv_i32 base = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv ptr = rl78_indirect_ptr(base, cpu_regs[RL78_GPREG_C]);

    rl78_gen_sb(ctx, cpu_regs[RL78_GPREG_A], ptr);

    return true;
}

static bool trans_MOV_indBCbase_i(DisasContext *ctx, arg_MOV_indBCbase_i *a)
{
    TCGv_i32 base = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv_i32 bc = rl78_load_rp(RL78_GPREG_BC);
    TCGv ptr = rl78_indirect_ptr(base, bc);

    rl78_gen_sb(ctx, tcg_constant_i32(a->imm), ptr);

    return true;
}

static bool trans_MOV_A_indBCbase(DisasContext *ctx, arg_MOV_A_indBCbase *a)
{
    TCGv_i32 base = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv_i32 bc = rl78_load_rp(RL78_GPREG_BC);
    TCGv ptr = rl78_indirect_ptr(base, bc);

    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], ptr);

    return true;
}

static bool trans_MOV_indBCbase_A(DisasContext *ctx, arg_MOV_indBCbase_A *a)
{
    TCGv_i32 base = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv_i32 bc = rl78_load_rp(RL78_GPREG_BC);
    TCGv ptr = rl78_indirect_ptr(base, bc);

    rl78_gen_sb(ctx, cpu_regs[RL78_GPREG_A], ptr);

    return true;
}

static bool trans_MOV_indSPoffset_i(DisasContext *ctx, arg_MOV_indSPoffset_i *a)
{
    TCGv_i32 ptr = rl78_indirect_ptr(cpu_sp, tcg_constant_i32(a->offset));

    rl78_gen_sb(ctx, tcg_constant_i32(a->imm), ptr);

    return true;
}

static bool trans_MOV_A_indSPoffset(DisasContext *ctx, arg_MOV_A_indSPoffset *a)
{
    TCGv_i32 ptr = rl78_indirect_ptr(cpu_sp, tcg_constant_i32(a->offset));

    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], ptr);

    return true;
}

static bool trans_MOV_indSPoffset_A(DisasContext *ctx, arg_MOV_indSPoffset_A *a)
{
    TCGv_i32 ptr = rl78_indirect_ptr(cpu_sp, tcg_constant_i32(a->offset));

    rl78_gen_sb(ctx, cpu_regs[RL78_GPREG_A], ptr);

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

static bool trans_MOV_X_addr(DisasContext *ctx, arg_MOV_X_addr *a)
{
    TCGv ptr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_X], ptr);
    return true;
}

static bool trans_MOV_A_addr(DisasContext *ctx, arg_MOV_A_addr *a)
{
    TCGv ptr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], ptr);
    return true;
}

static bool trans_MOV_B_addr(DisasContext *ctx, arg_MOV_B_addr *a)
{
    TCGv ptr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_B], ptr);
    return true;
}

static bool trans_MOV_C_addr(DisasContext *ctx, arg_MOV_C_addr *a)
{
    TCGv ptr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_C], ptr);
    return true;
}

static bool trans_XCH_A_X(DisasContext *ctx, arg_XCH_A_X *a)
{
    TCGv_i32 temp = tcg_temp_new_i32();
    tcg_gen_mov_i32(temp, cpu_regs[RL78_GPREG_A]);
    tcg_gen_mov_i32(cpu_regs[RL78_GPREG_A], cpu_regs[RL78_GPREG_X]);
    tcg_gen_mov_i32(cpu_regs[RL78_GPREG_X], temp);
    return true;
}

static bool trans_XCH_A_C(DisasContext *ctx, arg_XCH_A_C *a)
{
    TCGv_i32 temp = tcg_temp_new_i32();
    tcg_gen_mov_i32(temp, cpu_regs[RL78_GPREG_A]);
    tcg_gen_mov_i32(cpu_regs[RL78_GPREG_A], cpu_regs[RL78_GPREG_C]);
    tcg_gen_mov_i32(cpu_regs[RL78_GPREG_C], temp);
    return true;
}

static bool trans_XCH_A_B(DisasContext *ctx, arg_XCH_A_B *a)
{
    TCGv_i32 temp = tcg_temp_new_i32();
    tcg_gen_mov_i32(temp, cpu_regs[RL78_GPREG_A]);
    tcg_gen_mov_i32(cpu_regs[RL78_GPREG_A], cpu_regs[RL78_GPREG_B]);
    tcg_gen_mov_i32(cpu_regs[RL78_GPREG_B], temp);
    return true;
}

static bool trans_XCH_A_E(DisasContext *ctx, arg_XCH_A_E *a)
{
    TCGv_i32 temp = tcg_temp_new_i32();
    tcg_gen_mov_i32(temp, cpu_regs[RL78_GPREG_A]);
    tcg_gen_mov_i32(cpu_regs[RL78_GPREG_A], cpu_regs[RL78_GPREG_E]);
    tcg_gen_mov_i32(cpu_regs[RL78_GPREG_E], temp);
    return true;
}

static bool trans_XCH_A_D(DisasContext *ctx, arg_XCH_A_D *a)
{
    TCGv_i32 temp = tcg_temp_new_i32();
    tcg_gen_mov_i32(temp, cpu_regs[RL78_GPREG_A]);
    tcg_gen_mov_i32(cpu_regs[RL78_GPREG_A], cpu_regs[RL78_GPREG_D]);
    tcg_gen_mov_i32(cpu_regs[RL78_GPREG_D], temp);
    return true;
}

static bool trans_XCH_A_L(DisasContext *ctx, arg_XCH_A_L *a)
{
    TCGv_i32 temp = tcg_temp_new_i32();
    tcg_gen_mov_i32(temp, cpu_regs[RL78_GPREG_A]);
    tcg_gen_mov_i32(cpu_regs[RL78_GPREG_A], cpu_regs[RL78_GPREG_L]);
    tcg_gen_mov_i32(cpu_regs[RL78_GPREG_L], temp);
    return true;
}

static bool trans_XCH_A_H(DisasContext *ctx, arg_XCH_A_H *a)
{
    TCGv_i32 temp = tcg_temp_new_i32();
    tcg_gen_mov_i32(temp, cpu_regs[RL78_GPREG_A]);
    tcg_gen_mov_i32(cpu_regs[RL78_GPREG_A], cpu_regs[RL78_GPREG_H]);
    tcg_gen_mov_i32(cpu_regs[RL78_GPREG_H], temp);
    return true;
}

static bool trans_XCH_A_addr(DisasContext *ctx, arg_XCH_A_addr *a)
{
    TCGv addr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv_i32 temp = tcg_temp_new_i32();

    tcg_gen_mov_i32(temp, cpu_regs[RL78_GPREG_A]);
    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], addr);
    rl78_gen_sb(ctx, temp, addr);
    return true;
}

static bool trans_XCH_indDE(DisasContext *ctx, arg_XCH_indDE *a)
{
    TCGv_i32 temp = tcg_temp_new_i32();
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_DE);
    TCGv_i32 offset = tcg_constant_i32(0);
    TCGv ptr = rl78_indirect_ptr(base, offset);

    tcg_gen_mov_i32(temp, cpu_regs[RL78_GPREG_A]);
    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], ptr);
    rl78_gen_sb(ctx, temp, ptr);
    return true;
}

static bool trans_XCH_indDEoffset(DisasContext *ctx, arg_XCH_indDEoffset *a)
{
    TCGv_i32 temp = tcg_temp_new_i32();
    TCGv base = rl78_load_rp(RL78_GPREG_DE);
    TCGv ptr = rl78_indirect_ptr(base, tcg_constant_i32(a->offset));

    tcg_gen_mov_i32(temp, cpu_regs[RL78_GPREG_A]);
    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], ptr);
    rl78_gen_sb(ctx, temp, ptr);
    return true;
}

static bool trans_XCH_indHL(DisasContext *ctx, arg_XCH_indHL *a)
{
    TCGv_i32 temp = tcg_temp_new_i32();
    TCGv base = rl78_load_rp(RL78_GPREG_HL);
    TCGv ptr = rl78_indirect_ptr(base, tcg_constant_i32(0));

    tcg_gen_mov_i32(temp, cpu_regs[RL78_GPREG_A]);
    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], ptr);
    rl78_gen_sb(ctx, temp, ptr);
    return true;
}

static bool trans_XCH_indHLoffset(DisasContext *ctx, arg_XCH_indHLoffset *a)
{
    TCGv_i32 temp = tcg_temp_new_i32();
    TCGv base = rl78_load_rp(RL78_GPREG_HL);
    TCGv ptr = rl78_indirect_ptr(base, tcg_constant_i32(a->offset));

    tcg_gen_mov_i32(temp, cpu_regs[RL78_GPREG_A]);
    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], ptr);
    rl78_gen_sb(ctx, temp, ptr);
    return true;
}

static bool trans_XCH_indHL_B(DisasContext *ctx, arg_XCH_indHL_B *a)
{
    TCGv_i32 temp = tcg_temp_new_i32();
    TCGv base = rl78_load_rp(RL78_GPREG_HL);
    TCGv ptr = rl78_indirect_ptr(base, cpu_regs[RL78_GPREG_B]);

    tcg_gen_mov_i32(temp, cpu_regs[RL78_GPREG_A]);
    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], ptr);
    rl78_gen_sb(ctx, temp, ptr);
    return true;
}

static bool trans_XCH_indHL_C(DisasContext *ctx, arg_XCH_indHL_C *a)
{
    TCGv_i32 temp = tcg_temp_new_i32();
    TCGv base = rl78_load_rp(RL78_GPREG_HL);
    TCGv ptr = rl78_indirect_ptr(base, cpu_regs[RL78_GPREG_C]);

    tcg_gen_mov_i32(temp, cpu_regs[RL78_GPREG_A]);
    rl78_gen_lb(ctx, cpu_regs[RL78_GPREG_A], ptr);
    rl78_gen_sb(ctx, temp, ptr);
    return true;
}

static bool trans_ONEB_r(DisasContext *ctx, arg_ONEB_r *a)
{
    const RL78GPRegister r = a->r; 

    tcg_gen_movi_i32(cpu_regs[r], 0x01);

    return true;
}

static bool trans_ONEB_addr(DisasContext *ctx, arg_ONEB_addr *a)
{
    TCGv_i32 addr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    rl78_gen_sb(ctx, tcg_constant_i32(0x01), addr);

    return true;
}

static bool trans_CLRB_r(DisasContext *ctx, arg_CLRB_r *a)
{
    const RL78GPRegister r = a->r; 

    tcg_gen_movi_i32(cpu_regs[r], 0x00);

    return true;
}

static bool trans_CLRB_addr(DisasContext *ctx, arg_CLRB_addr *a)
{
    TCGv_i32 addr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    rl78_gen_sb(ctx, tcg_constant_i32(0x00), addr);

    return true;
}

static bool trans_MOVS_indHLoffset_X(DisasContext *ctx, arg_MOVS_indHLoffset_X *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_HL);
    TCGv_i32 offset = tcg_constant_i32(a->offset);
    TCGv ptr = rl78_indirect_ptr(base, offset);
    TCGv_i32 is_a_zero = tcg_temp_new_i32();
    TCGv_i32 is_x_zero = tcg_temp_new_i32();

    rl78_gen_sb(ctx, cpu_regs[RL78_GPREG_X], ptr);
    tcg_gen_setcondi_i32(TCG_COND_EQ, is_a_zero, cpu_regs[RL78_GPREG_A], 0);
    tcg_gen_setcondi_i32(TCG_COND_EQ, is_x_zero, cpu_regs[RL78_GPREG_X], 0);
    tcg_gen_mov_i32(cpu_psw_z, is_x_zero);
    tcg_gen_or_i32(cpu_psw_cy, is_a_zero, is_x_zero);

    return true;
}

static bool trans_MOVW_rp_i(DisasContext *ctx, arg_MOVW_rp_i *a)
{
    const uint32_t imm = a->datal | (a->datah << 8);
    const RL78GPRegister rp = a->rp * 2;

    rl78_store_rp(rp, tcg_constant_i32(imm));
    return true;
}

static bool trans_MOVW_SP_i(DisasContext *ctx, arg_MOVW_SP_i *a)
{
    const uint32_t data = a->datal | (a->datah << 8);
    TCGv_i32 imm = tcg_constant_i32(data & 0xFFFE);

    tcg_gen_mov_i32(cpu_sp, imm);

    return true;
}

static bool trans_MOVW_AX_SP(DisasContext *ctx, arg_MOVW_AX_SP *a){
    rl78_store_rp(RL78_GPREG_AX, cpu_sp);
    return true;
}

static bool trans_MOVW_BC_SP(DisasContext *ctx, arg_MOVW_BC_SP *a)
{
    rl78_store_rp(RL78_GPREG_BC, cpu_sp);
    return true;
}

static bool trans_MOVW_DE_SP(DisasContext *ctx, arg_MOVW_DE_SP *a)
{
    rl78_store_rp(RL78_GPREG_DE, cpu_sp);
    return true;
}

static bool trans_MOVW_HL_SP(DisasContext *ctx, arg_MOVW_HL_SP *a)
{
    rl78_store_rp(RL78_GPREG_HL, cpu_sp);
    return true;
}

static bool trans_MOVW_addr_AX(DisasContext *ctx, arg_MOVW_addr_AX *a)
{
    TCGv_i32 ptr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv_i32 data = rl78_load_rp(RL78_GPREG_AX);

    rl78_gen_sw(ctx, data, ptr);
    
    return true;
}

static bool trans_MOVW_rp_addr(DisasContext *ctx, RL78GPRegister rp, TCGv_i32 ptr)
{
    TCGv_i32 data = tcg_temp_new_i32();
    rl78_gen_lw(ctx, data, ptr);
    rl78_store_rp(rp, data);

    return true;
}

static bool trans_MOVW_AX_addr(DisasContext *ctx, arg_MOVW_AX_addr *a)
{
    TCGv_i32 ptr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    return trans_MOVW_rp_addr(ctx, RL78_GPREG_AX, ptr);
}

static bool trans_MOVW_BC_addr(DisasContext *ctx, arg_MOVW_BC_addr *a)
{
    TCGv_i32 ptr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    return trans_MOVW_rp_addr(ctx, RL78_GPREG_BC, ptr);
}

static bool trans_MOVW_DE_addr(DisasContext *ctx, arg_MOVW_DE_addr *a)
{
    TCGv_i32 ptr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    return trans_MOVW_rp_addr(ctx, RL78_GPREG_DE, ptr);
}

static bool trans_MOVW_HL_addr(DisasContext *ctx, arg_MOVW_HL_addr *a)
{
    TCGv_i32 ptr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    return trans_MOVW_rp_addr(ctx, RL78_GPREG_HL, ptr);
}

static bool trans_MOVW_AX_indDE(DisasContext *ctx, arg_MOVW_AX_indDE *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_DE);
    TCGv ptr = rl78_indirect_ptr(base, tcg_constant_i32(0));
    TCGv_i32 data = tcg_temp_new_i32();
    rl78_gen_lw(ctx, data, ptr);
    rl78_store_rp(RL78_GPREG_AX, data);
    return true;
}

static bool trans_MOVW_AX_indHL(DisasContext *ctx, arg_MOVW_AX_indHL *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_HL);
    TCGv ptr = rl78_indirect_ptr(base, tcg_constant_i32(0));
    TCGv_i32 data = tcg_temp_new_i32();
    rl78_gen_lw(ctx, data, ptr);
    rl78_store_rp(RL78_GPREG_AX, data);
    return true;
}

static bool trans_MOVW_indDE_AX(DisasContext *ctx, arg_MOVW_indDE_AX *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_DE);
    TCGv ptr = rl78_indirect_ptr(base, tcg_constant_i32(0));
    TCGv_i32 data = rl78_load_rp(RL78_GPREG_AX);
    rl78_gen_sw(ctx, data, ptr);
    return true;
}

static bool trans_MOVW_indHL_AX(DisasContext *ctx, arg_MOVW_indHL_AX *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_HL);
    TCGv ptr = rl78_indirect_ptr(base, tcg_constant_i32(0));
    TCGv_i32 data = rl78_load_rp(RL78_GPREG_AX);
    rl78_gen_sw(ctx, data, ptr);
    return true;
}

static bool trans_MOVW_AX_indDEoffset(DisasContext *ctx, arg_MOVW_AX_indDEoffset *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_DE);
    TCGv ptr = rl78_indirect_ptr(base, tcg_constant_i32(a->offset));
    TCGv_i32 data = tcg_temp_new_i32();
    rl78_gen_lw(ctx, data, ptr);
    rl78_store_rp(RL78_GPREG_AX, data);
    return true;
}

static bool trans_MOVW_AX_indHLoffset(DisasContext *ctx, arg_MOVW_AX_indHLoffset *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_HL);
    TCGv ptr = rl78_indirect_ptr(base, tcg_constant_i32(a->offset));
    TCGv_i32 data = tcg_temp_new_i32();
    rl78_gen_lw(ctx, data, ptr);
    rl78_store_rp(RL78_GPREG_AX, data);
    return true;
}

static bool trans_MOVW_AX_indSPoffset(DisasContext *ctx, arg_MOVW_AX_indSPoffset *a)
{
    TCGv ptr = rl78_indirect_ptr(cpu_sp, tcg_constant_i32(a->offset));
    TCGv_i32 data = tcg_temp_new_i32();
    rl78_gen_lw(ctx, data, ptr);
    rl78_store_rp(RL78_GPREG_AX, data);

    return true;
}

static bool trans_MOVW_indDEoffset_AX(DisasContext *ctx, arg_MOVW_indDEoffset_AX *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_DE);
    TCGv ptr = rl78_indirect_ptr(base, tcg_constant_i32(a->offset));
    TCGv_i32 data = rl78_load_rp(RL78_GPREG_AX);
    rl78_gen_sw(ctx, data, ptr);

    return true;
}

static bool trans_MOVW_indHLoffset_AX(DisasContext *ctx, arg_MOVW_indHLoffset_AX *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_HL);
    TCGv ptr = rl78_indirect_ptr(base, tcg_constant_i32(a->offset));
    TCGv_i32 data = rl78_load_rp(RL78_GPREG_AX);
    rl78_gen_sw(ctx, data, ptr);

    return true;
}

static bool trans_MOVW_indSPoffset_AX(DisasContext *ctx, arg_MOVW_indSPoffset_AX *a)
{
    TCGv ptr = rl78_indirect_ptr(cpu_sp, tcg_constant_i32(a->offset));
    TCGv_i32 data = rl78_load_rp(RL78_GPREG_AX);
    rl78_gen_sw(ctx, data, ptr);

    return true;
}

static bool trans_MOVW_AX_indBbase(DisasContext *ctx, arg_MOVW_AX_indBbase *a)
{
    TCGv_i32 data = tcg_temp_new_i32();
    const uint32_t base = a->adrl | (a->adrh << 8);
    TCGv ptr = rl78_indirect_ptr(tcg_constant_i32(base), cpu_regs[RL78_GPREG_B]);
    rl78_gen_lw(ctx, data, ptr);
    rl78_store_rp(RL78_GPREG_AX, data);

    return true;
}

static bool trans_MOVW_AX_indCbase(DisasContext *ctx, arg_MOVW_AX_indCbase *a)
{
    TCGv_i32 data = tcg_temp_new_i32();
    const uint32_t base = a->adrl | (a->adrh << 8);
    TCGv ptr = rl78_indirect_ptr(tcg_constant_i32(base), cpu_regs[RL78_GPREG_C]);
    rl78_gen_lw(ctx, data, ptr);
    rl78_store_rp(RL78_GPREG_AX, data);

    return true;
}

static bool trans_MOVW_AX_indBCbase(DisasContext *ctx, arg_MOVW_AX_indBCbase *a)
{
    TCGv_i32 data = tcg_temp_new_i32();
    TCGv_i32 offset = rl78_load_rp(RL78_GPREG_BC);
    const uint32_t base = a->adrl | (a->adrh << 8); 
    TCGv ptr = rl78_indirect_ptr(tcg_constant_i32(base), offset);
    rl78_gen_lw(ctx, data, ptr);
    rl78_store_rp(RL78_GPREG_AX, data);

    return true;
}

static bool trans_MOVW_indBbase_AX(DisasContext *ctx, arg_MOVW_indBbase_AX *a)
{
    TCGv_i32 data = rl78_load_rp(RL78_GPREG_AX);
    TCGv_i32 offset = cpu_regs[RL78_GPREG_B];
    const uint32_t base = a->adrl | (a->adrh << 8);
    TCGv ptr = rl78_indirect_ptr(tcg_constant_i32(base), offset);
    rl78_gen_sw(ctx, data, ptr);

    return true;
}

static bool trans_MOVW_indCbase_AX(DisasContext *ctx, arg_MOVW_indCbase_AX *a)
{
    TCGv_i32 data = rl78_load_rp(RL78_GPREG_AX);
    TCGv_i32 offset = cpu_regs[RL78_GPREG_C];
    const uint32_t base = a->adrl | (a->adrh << 8);
    TCGv ptr = rl78_indirect_ptr(tcg_constant_i32(base), offset);
    rl78_gen_sw(ctx, data, ptr);

    return true;
}

static bool trans_MOVW_indBCbase_AX(DisasContext *ctx, arg_MOVW_indBCbase_AX *a)
{
    TCGv_i32 data = rl78_load_rp(RL78_GPREG_AX);
    TCGv_i32 offset = rl78_load_rp(RL78_GPREG_BC);
    const uint32_t base = a->adrl | (a->adrh << 8);
    TCGv ptr = rl78_indirect_ptr(tcg_constant_i32(base), offset);
    rl78_gen_sw(ctx, data, ptr);

    return true;
}


static bool rl78_MOVW_rp_rp(RL78GPRegister dst, RL78GPRegister src)
{
    tcg_gen_mov_i32(cpu_regs[dst], cpu_regs[src]);
    tcg_gen_mov_i32(cpu_regs[dst + 1], cpu_regs[src + 1]);
    return true;
}

static bool trans_MOVW_AX_BC(DisasContext *ctx, arg_MOVW_AX_BC *a)
{
    return rl78_MOVW_rp_rp(RL78_GPREG_AX, RL78_GPREG_BC);
}

static bool trans_MOVW_AX_DE(DisasContext *ctx, arg_MOVW_AX_DE *a)
{
    return rl78_MOVW_rp_rp(RL78_GPREG_AX, RL78_GPREG_DE);
}
static bool trans_MOVW_AX_HL(DisasContext *ctx, arg_MOVW_AX_HL *a)
{
    return rl78_MOVW_rp_rp(RL78_GPREG_AX, RL78_GPREG_HL);
}
static bool trans_MOVW_BC_AX(DisasContext *ctx, arg_MOVW_BC_AX *a)
{
    return rl78_MOVW_rp_rp(RL78_GPREG_BC, RL78_GPREG_AX);
}
static bool trans_MOVW_DE_AX(DisasContext *ctx, arg_MOVW_DE_AX *a)
{
    return rl78_MOVW_rp_rp(RL78_GPREG_DE, RL78_GPREG_AX);
}
static bool trans_MOVW_HL_AX(DisasContext *ctx, arg_MOVW_HL_AX *a)
{
    return rl78_MOVW_rp_rp(RL78_GPREG_HL, RL78_GPREG_AX);
}
static bool trans_MOVW_SP_AX(DisasContext *ctx, arg_MOVW_SP_AX *a)
{
    TCGv_i32 ax = rl78_load_rp(RL78_GPREG_AX);
    tcg_gen_andi_i32(ax, ax, 0xFFFE);
    tcg_gen_mov_i32(cpu_sp, ax);
    return true;
}

static bool rl78_XCHW_rp_rp(RL78GPRegister rp)
{
    TCGv_i32 ax = rl78_load_rp(RL78_GPREG_AX);
    TCGv_i32 rpdata = rl78_load_rp(rp);
    rl78_store_rp(rp, ax);
    rl78_store_rp(RL78_GPREG_AX, rpdata);
    return true;
}

static bool trans_XCHW_AX_BC(DisasContext *ctx, arg_XCHW_AX_BC *a)
{
    return rl78_XCHW_rp_rp(RL78_GPREG_BC);
}

static bool trans_XCHW_AX_DE(DisasContext *ctx, arg_XCHW_AX_DE *a)
{
    return rl78_XCHW_rp_rp(RL78_GPREG_DE);
}

static bool trans_XCHW_AX_HL(DisasContext *ctx, arg_XCHW_AX_HL *a)
{
    return rl78_XCHW_rp_rp(RL78_GPREG_HL);
}

static bool trans_ONEW_AX(DisasContext *ctx, arg_ONEW_AX *a)
{
    rl78_store_rp(RL78_GPREG_AX, tcg_constant_i32(1));
    return true;
}

static bool trans_ONEW_BC(DisasContext *ctx, arg_ONEW_BC *a)
{
    rl78_store_rp(RL78_GPREG_BC, tcg_constant_i32(1));
    return true;
}

static bool trans_CLRW_AX(DisasContext *ctx, arg_CLRW_AX *a)
{
    rl78_store_rp(RL78_GPREG_AX, tcg_constant_i32(0));
    return true;
}

static bool trans_CLRW_BC(DisasContext *ctx, arg_CLRW_BC *a)
{
    rl78_store_rp(RL78_GPREG_BC, tcg_constant_i32(0));
    return true;
}

static bool rl78_gen_add(TCGv_i32 ret, TCGv_i32 op)
{
    TCGv_i32 ret_temp;
    TCGv_i32 op0_half, op1_half, ret_half;

    ret_temp = tcg_temp_new_i32();
    op0_half = tcg_temp_new_i32();
    op1_half = tcg_temp_new_i32();
    ret_half = tcg_temp_new_i32();

    tcg_gen_add_i32(ret_temp, ret, op);

    tcg_gen_andi_i32(op0_half, ret, 0x0F);
    tcg_gen_andi_i32(op1_half, op, 0x0F);
    tcg_gen_add_i32(ret_half, op0_half, op1_half);

    tcg_gen_andi_i32(ret, ret_temp, 0xFF);
    tcg_gen_setcondi_i32(TCG_COND_GEU, cpu_psw_cy, ret_temp, 0x100);
    tcg_gen_setcondi_i32(TCG_COND_GEU, cpu_psw_ac, ret_half, 0x10);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_z, ret, 0);

    return true;
}

static bool rl78_gen_addc(TCGv_i32 ret, TCGv_i32 op)
{
    TCGv_i32 ret_temp;
    TCGv_i32 op0_half, op1_half, ret_half;

    ret_temp = tcg_temp_new_i32();
    op0_half = tcg_temp_new_i32();
    op1_half = tcg_temp_new_i32();
    ret_half = tcg_temp_new_i32();

    tcg_gen_add_i32(ret_temp, ret, op);
    tcg_gen_add_i32(ret_temp, ret_temp, cpu_psw_cy);

    tcg_gen_andi_i32(op0_half, ret, 0x0F);
    tcg_gen_andi_i32(op1_half, op, 0x0F);
    tcg_gen_add_i32(ret_half, op0_half, op1_half);
    tcg_gen_add_i32(ret_half, ret_half, cpu_psw_cy);

    tcg_gen_andi_i32(ret, ret_temp, 0xFF);
    tcg_gen_setcondi_i32(TCG_COND_GEU, cpu_psw_cy, ret_temp, 0x100);
    tcg_gen_setcondi_i32(TCG_COND_GEU, cpu_psw_ac, ret_half, 0x10);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_z, ret, 0);

    return true;
}

static bool rl78_gen_sub(TCGv_i32 ret, TCGv_i32 op)
{
    TCGv_i32 ret_temp;
    TCGv_i32 op0_half, op1_half, ret_half;

    ret_temp = tcg_temp_new_i32();
    op0_half = tcg_temp_new_i32();
    op1_half = tcg_temp_new_i32();
    ret_half = tcg_temp_new_i32();

    tcg_gen_sub_i32(ret_temp, ret, op);
    tcg_gen_andi_i32(ret_temp, ret_temp, 0xFF);

    tcg_gen_andi_i32(op0_half, ret, 0x0F);
    tcg_gen_andi_i32(op1_half, op, 0x0F);
    tcg_gen_sub_i32(ret_half, op0_half, op1_half);

    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_psw_cy, ret, op);
    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_psw_ac, op0_half, op1_half);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_z, ret_temp, 0);
    tcg_gen_mov_i32(ret, ret_temp);

    return true;
}

static bool rl78_gen_subc(TCGv_i32 ret, TCGv_i32 op)
{
    TCGv_i32 subc_op, ret_temp;
    TCGv_i32 op0_half, op1_half;

    subc_op = tcg_temp_new_i32();
    ret_temp = tcg_temp_new_i32();
    op0_half = tcg_temp_new_i32();
    op1_half = tcg_temp_new_i32();

    tcg_gen_add_i32(subc_op, op, cpu_psw_cy);
    tcg_gen_sub_i32(ret_temp, ret, subc_op);
    tcg_gen_andi_i32(ret_temp, ret_temp, 0xFF);

    tcg_gen_andi_i32(op0_half, ret, 0x0F);
    tcg_gen_andi_i32(op1_half, op, 0x0F);
    tcg_gen_add_i32(op1_half, op1_half, cpu_psw_cy);

    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_psw_cy, ret, subc_op);
    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_psw_ac, op0_half, op1_half);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_z, ret_temp, 0);

    tcg_gen_mov_i32(ret, ret_temp);

    return true;
}

static bool rl78_gen_and(TCGv_i32 ret, TCGv_i32 op)
{
    tcg_gen_and_i32(ret, ret, op);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_z, ret, 0);
    return true;
}

static bool rl78_gen_or(TCGv_i32 ret, TCGv_i32 op)
{
    tcg_gen_or_i32(ret, ret, op);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_z, ret, 0);
    return true;
}

static bool rl78_gen_xor(TCGv_i32 ret, TCGv_i32 op)
{
    tcg_gen_xor_i32(ret, ret, op);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_z, ret, 0);
    return true;
}

static bool rl78_gen_cmp(TCGv_i32 ret, TCGv_i32 op)
{
    TCGv_i32 ret_temp;
    TCGv_i32 op0_half, op1_half;

    ret_temp = tcg_temp_new_i32();
    op0_half = tcg_temp_new_i32();
    op1_half = tcg_temp_new_i32();

    tcg_gen_sub_i32(ret_temp, ret, op);

    tcg_gen_andi_i32(op0_half, ret, 0x0F);
    tcg_gen_andi_i32(op1_half, op, 0x0F);

    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_z, ret_temp, 0);
    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_psw_cy, ret, op);
    tcg_gen_setcond_i32(TCG_COND_LTU, cpu_psw_ac, op0_half, op1_half);

    return true;
}

static bool (*rl78_arith_op(int op))(TCGv_i32, TCGv_i32)
{
    switch(op) {
        case 0: return rl78_gen_add;
        case 1: return rl78_gen_addc;
        case 2: return rl78_gen_sub;
        case 3: return rl78_gen_subc;
        case 4: return rl78_gen_cmp;
        case 5: return rl78_gen_and;
        case 6: return rl78_gen_or;
        case 7: return rl78_gen_xor;
    }

    return NULL;
}

static bool trans_Arith_A_i(DisasContext *ctx, arg_Arith_A_i *a)
{
    return rl78_arith_op(a->op)(cpu_regs[RL78_GPREG_A], tcg_constant_i32(a->imm));
}

#define ARITH_R_R(r0, r1) \
static bool trans_Arith_##r0##_##r1(DisasContext *ctx, arg_Arith_##r0##_##r1 *a) \
{ \
    return rl78_arith_op(a->op)(cpu_regs[RL78_GPREG_##r0], cpu_regs[RL78_GPREG_##r1]); \
} \

ARITH_R_R(A, X)
ARITH_R_R(A, C)
ARITH_R_R(A, B)
ARITH_R_R(A, E)
ARITH_R_R(A, D)
ARITH_R_R(A, L)
ARITH_R_R(A, H)

ARITH_R_R(X, A)
ARITH_R_R(A, A)
ARITH_R_R(C, A)
ARITH_R_R(B, A)
ARITH_R_R(E, A)
ARITH_R_R(D, A)
ARITH_R_R(L, A)
ARITH_R_R(H, A)

static bool trans_Arith_A_addr(DisasContext *ctx, arg_Arith_A_addr *a)
{
    TCGv_i32 ptr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv_i32 value = tcg_temp_new_i32();
    rl78_gen_lb(ctx, value, ptr);
    return rl78_arith_op(a->op)(cpu_regs[RL78_GPREG_A], value);
}

static bool trans_Arith_A_indHL(DisasContext *ctx, arg_Arith_A_indHL *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_HL);
    TCGv ptr = rl78_indirect_ptr(base, tcg_constant_i32(0x00));
    TCGv_i32 value = tcg_temp_new_i32();
    rl78_gen_lb(ctx, value, ptr);
    return rl78_arith_op(a->op)(cpu_regs[RL78_GPREG_A], value);
}

static bool trans_Arith_A_indHLoffset(DisasContext *ctx, arg_Arith_A_indHLoffset *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_HL);
    TCGv_i32 ptr = rl78_indirect_ptr(base, tcg_constant_i32(a->offset));
    TCGv_i32 value = tcg_temp_new_i32();
    rl78_gen_lb(ctx, value, ptr);
    return rl78_arith_op(a->op)(cpu_regs[RL78_GPREG_A], value);
}

static bool trans_Arith_A_indHL_B(DisasContext *ctx, arg_Arith_A_indHL_B *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_HL);
    TCGv ptr = rl78_indirect_ptr(base, cpu_regs[RL78_GPREG_B]);
    TCGv_i32 value = tcg_temp_new_i32();
    rl78_gen_lb(ctx, value, ptr);
    return rl78_arith_op(a->op)(cpu_regs[RL78_GPREG_A], value);
}

static bool trans_Arith_A_indHL_C(DisasContext *ctx, arg_Arith_A_indHL_C *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_HL);
    TCGv ptr = rl78_indirect_ptr(base, cpu_regs[RL78_GPREG_C]);
    TCGv_i32 value = tcg_temp_new_i32();
    rl78_gen_lb(ctx, value, ptr);
    return rl78_arith_op(a->op)(cpu_regs[RL78_GPREG_A], value);
}

static bool trans_CMP0_r(DisasContext *ctx, arg_CMP0_r *a)
{
    const int reg = a->op;
    return rl78_gen_cmp(cpu_regs[reg], tcg_constant_i32(0));
}

static bool trans_CMP0_addr(DisasContext *ctx, arg_CMP0_addr *a)
{
    TCGv_i32 ptr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv_i32 value = tcg_temp_new_i32();
    rl78_gen_lb(ctx, value, ptr);
    return rl78_gen_cmp(value, tcg_constant_i32(0));
}

static bool trans_CMPS_X_indHLoffset(DisasContext *ctx, arg_CMPS_X_indHLoffset *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_HL);
    TCGv ptr = rl78_indirect_ptr(base, tcg_constant_i32(a->offset));
    TCGv_i32 value = tcg_temp_new_i32();
    rl78_gen_lb(ctx, value, ptr);
    return rl78_gen_cmp(cpu_regs[RL78_GPREG_X], value);
}

static bool rl78_gen_addw(TCGv_i32 op)
{
    TCGv_i32 ax = rl78_load_rp(RL78_GPREG_AX);
    TCGv_i32 temp = tcg_temp_new_i32();
    TCGv_i32 ret = tcg_temp_new_i32();
    tcg_gen_add_i32(temp, ax, op);
    tcg_gen_andi_i32(ret, temp, 0xFFFF);

    tcg_gen_setcondi_i32(TCG_COND_GEU, cpu_psw_cy, temp, 0x10000);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_z, ret, 0x0000);
    tcg_gen_movi_i32(cpu_psw_ac, 0);

    rl78_store_rp(RL78_GPREG_AX, ret);

    return true;
}

static bool trans_ADDW_AX_i(DisasContext *ctx, arg_ADDW_AX_i *a)
{
    const uint32_t imm = a->datal | (a->datah << 8);
    return rl78_gen_addw(tcg_constant_i32(imm));
}

static bool trans_ADDW_AX_rp(DisasContext *ctx, arg_ADDW_AX_rp *a)
{
    const RL78GPRegister rp = a->rp * 2;
    TCGv_i32 op = rl78_load_rp(rp);

    return rl78_gen_addw(op);
}

static bool trans_ADDW_AX_addr(DisasContext *ctx, arg_ADDW_AX_addr *a)
{
    TCGv_i32 addr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv_i32 op = tcg_temp_new_i32();
    rl78_gen_lw(ctx, op ,addr);

    return rl78_gen_addw(op);
}

static bool trans_ADDW_AX_indHLoffset(DisasContext *ctx, arg_ADDW_AX_indHLoffset *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_HL);
    TCGv_i32 ptr = rl78_indirect_ptr(base, tcg_constant_i32(a->offset));
    TCGv_i32 op = tcg_temp_new_i32();
    rl78_gen_lw(ctx, op, ptr);

    return rl78_gen_addw(op);
}

static bool rl78_gen_subw(TCGv_i32 op)
{
    TCGv_i32 ax = rl78_load_rp(RL78_GPREG_AX);
    TCGv_i32 temp = tcg_temp_new_i32();
    TCGv_i32 ret = tcg_temp_new_i32();
    tcg_gen_sub_i32(temp, ax, op);
    tcg_gen_andi_i32(ret, temp, 0xFFFF);
 
    tcg_gen_setcondi_i32(TCG_COND_GEU, cpu_psw_cy, temp, 0x10000);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_z, ret, 0x0000);
    tcg_gen_movi_i32(cpu_psw_ac, 0);

    rl78_store_rp(RL78_GPREG_AX, ret);
    return true;
}

static bool trans_SUBW_AX_i(DisasContext *ctx, arg_SUBW_AX_i *a)
{
    const uint32_t imm = a->datal | (a->datah << 8);
    return rl78_gen_subw(tcg_constant_i32(imm));
}

static bool trans_SUBW_AX_rp(DisasContext *ctx, arg_SUBW_AX_rp *a)
{
    TCGv_i32 rp = rl78_load_rp(a->rp * 2);
    return rl78_gen_subw(rp);
}

static bool trans_SUBW_AX_addr(DisasContext *ctx, arg_SUBW_AX_addr *a)
{
    TCGv_i32 ptr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv_i32 op = tcg_temp_new_i32();
    rl78_gen_lw(ctx, op, ptr);

    return rl78_gen_subw(op);
}

static bool trans_SUBW_AX_indHLoffset(DisasContext *ctx, arg_SUBW_AX_indHLoffset *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_HL);
    TCGv_i32 ptr = rl78_indirect_ptr(base, tcg_constant_i32(a->offset));
    TCGv_i32 op = tcg_temp_new_i32();
    rl78_gen_lw(ctx, op, ptr);
    
    return rl78_gen_subw(op);
}

static bool rl78_gen_cmpw(TCGv_i32 op)
{
    TCGv_i32 ax = rl78_load_rp(RL78_GPREG_AX);
    tcg_gen_sub_i32(ax, ax, op);
    
    tcg_gen_setcondi_i32(TCG_COND_GEU, cpu_psw_cy, ax, 0x10000);
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_z, ax, 0x0000);
    tcg_gen_movi_i32(cpu_psw_ac, 0);

    return true;
}

static bool trans_CMPW_AX_i(DisasContext *ctx, arg_CMPW_AX_i *a)
{
    const uint32_t imm = a->datal | (a->datah << 8);
    return rl78_gen_cmpw(tcg_constant_i32(imm));
}

static bool trans_CMPW_AX_rp(DisasContext *ctx, RL78GPRegister rp)
{
    TCGv_i32 op = rl78_load_rp(rp);
    return rl78_gen_cmpw(op);
}

static bool trans_CMPW_AX_BC(DisasContext *ctx, arg_CMPW_AX_BC *a)
{
    return trans_CMPW_AX_rp(ctx, RL78_GPREG_BC);
}

static bool trans_CMPW_AX_DE(DisasContext *ctx, arg_CMPW_AX_DE *a)
{
    return trans_CMPW_AX_rp(ctx, RL78_GPREG_DE);
}

static bool trans_CMPW_AX_HL(DisasContext *ctx, arg_CMPW_AX_HL *a)
{
    return trans_CMPW_AX_rp(ctx, RL78_GPREG_HL);
}

static bool trans_CMPW_AX_addr(DisasContext *ctx, arg_CMPW_AX_addr *a)
{
    TCGv_i32 ptr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv_i32 op = tcg_temp_new_i32();
    rl78_gen_lw(ctx, op, ptr);

    return rl78_gen_cmpw(op);
}

static bool trans_CMPW_AX_indHLoffset(DisasContext *ctx, arg_CMPW_AX_indHLoffset *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_HL);
    TCGv_i32 ptr = rl78_indirect_ptr(base, tcg_constant_i32(a->offset));
    TCGv_i32 op = tcg_temp_new_i32();
    rl78_gen_lw(ctx, op, ptr);

    return rl78_gen_cmpw(op);
}

static bool trans_MULU(DisasContext *ctx, arg_MULU *_a)
{
    TCGv_i32 ax = tcg_temp_new_i32();
    TCGv_i32 a = cpu_regs[RL78_GPREG_A];
    TCGv_i32 x = cpu_regs[RL78_GPREG_X];

    tcg_gen_mul_i32(ax, a, x);
    rl78_store_rp(RL78_GPREG_AX, ax);
    return true;
}

static bool trans_MULHU(DisasContext *ctx, arg_MULHU *a)
{
    TCGv_i32 ax = rl78_load_rp(RL78_GPREG_AX);
    TCGv_i32 bc = rl78_load_rp(RL78_GPREG_BC);
    TCGv_i32 ret = tcg_temp_new_i32();
    TCGv_i32 ret_ax = tcg_temp_new_i32();
    TCGv_i32 ret_bc = tcg_temp_new_i32();

    tcg_gen_mul_i32(ret, ax, bc);

    tcg_gen_extract_i32(ret_ax, ret, 0, 16);
    tcg_gen_extract_i32(ret_bc, ret, 16, 16);

    rl78_store_rp(RL78_GPREG_AX, ret_ax);
    rl78_store_rp(RL78_GPREG_BC, ret_bc);

    return true;
}
static bool trans_MULH(DisasContext *ctx, arg_MULH *a)
{
    TCGv_i32 ax = rl78_load_rp(RL78_GPREG_AX);
    TCGv_i32 bc = rl78_load_rp(RL78_GPREG_BC);
    TCGv_i32 ax_s = tcg_temp_new_i32();
    TCGv_i32 bc_s = tcg_temp_new_i32();
    TCGv_i32 ret = tcg_temp_new_i32();
    TCGv_i32 ret_ax = tcg_temp_new_i32();
    TCGv_i32 ret_bc = tcg_temp_new_i32();
    
    tcg_gen_ext16s_i32(ax_s, ax);
    tcg_gen_ext16s_i32(bc_s, bc);
    tcg_gen_mul_i32(ret, ax_s, bc_s);
    tcg_gen_extract_i32(ret_ax, ret, 0, 16);
    tcg_gen_extract_i32(ret_bc, ret, 16, 16);

    rl78_store_rp(RL78_GPREG_AX, ret_ax);
    rl78_store_rp(RL78_GPREG_BC, ret_bc);

    return true;
}

static bool trans_DIVHU(DisasContext *ctx, arg_DIVHU *a)
{
    TCGv_i32 ax = rl78_load_rp(RL78_GPREG_AX);
    TCGv_i32 de = rl78_load_rp(RL78_GPREG_DE);
    TCGv_i32 ret_ax = tcg_temp_new_i32();
    TCGv_i32 ret_de = tcg_temp_new_i32();
    TCGLabel* nodiv = gen_new_label();
    TCGLabel* enddiv = gen_new_label();

    tcg_gen_brcondi_i32(TCG_COND_EQ, de, 0x0000, nodiv);
    
    tcg_gen_divu_i32(ret_ax, ax, de);
    tcg_gen_remu_i32(ret_de, ax, de);
    tcg_gen_br(enddiv);

    gen_set_label(nodiv);
    tcg_gen_movi_i32(ret_ax, 0xFFFF);
    tcg_gen_mov_i32(ret_de, ax);

    gen_set_label(enddiv);
    rl78_store_rp(RL78_GPREG_AX, ret_ax);
    rl78_store_rp(RL78_GPREG_DE, ret_de);

    return true;
}

static bool trans_DIVWU(DisasContext *ctx, arg_DIVWU *a)
{
    TCGv_i32 ax = rl78_load_rp(RL78_GPREG_AX);
    TCGv_i32 bc = rl78_load_rp(RL78_GPREG_BC);
    TCGv_i32 de = rl78_load_rp(RL78_GPREG_DE);
    TCGv_i32 hl = rl78_load_rp(RL78_GPREG_HL);
    TCGv_i32 bcax = tcg_temp_new_i32();
    TCGv_i32 hlde = tcg_temp_new_i32();
    TCGv_i32 ret_div = tcg_temp_new_i32();
    TCGv_i32 ret_rem = tcg_temp_new_i32();
    TCGv_i32 ret_ax = tcg_temp_new_i32();
    TCGv_i32 ret_bc = tcg_temp_new_i32();
    TCGv_i32 ret_de = tcg_temp_new_i32();
    TCGv_i32 ret_hl = tcg_temp_new_i32();
    TCGLabel* nodiv = gen_new_label();
    TCGLabel* enddiv = gen_new_label();

    // 初期化しないとTCGのコードジェネレーション部分でアサートに引っかかる
    tcg_gen_movi_i32(bcax, 0);
    tcg_gen_movi_i32(hlde, 0);
    tcg_gen_deposit_i32(bcax, bcax, ax, 0, 16);
    tcg_gen_deposit_i32(bcax, bcax, bc, 16, 16);
    tcg_gen_deposit_i32(hlde, hlde, de, 0, 16);
    tcg_gen_deposit_i32(hlde, hlde, hl, 16, 16);

    tcg_gen_brcondi_i32(TCG_COND_EQ, hlde, 0, nodiv);
    tcg_gen_divu_i32(ret_div, bcax, hlde);
    tcg_gen_remu_i32(ret_rem, bcax, hlde);
    tcg_gen_br(enddiv);

    gen_set_label(nodiv);
    tcg_gen_movi_i32(ret_div, 0xFFFFFFFF);
    tcg_gen_mov_i32(ret_rem, bcax);

    gen_set_label(enddiv);
    tcg_gen_extract_i32(ret_ax, ret_div, 0, 16);
    tcg_gen_extract_i32(ret_bc, ret_div, 16, 16);
    tcg_gen_extract_i32(ret_de, ret_rem, 0, 16);
    tcg_gen_extract_i32(ret_hl, ret_rem, 16, 16);
    rl78_store_rp(RL78_GPREG_AX, ret_ax);
    rl78_store_rp(RL78_GPREG_BC, ret_bc);
    rl78_store_rp(RL78_GPREG_DE, ret_de);
    rl78_store_rp(RL78_GPREG_HL, ret_hl);

    return true;
}

static void rl78_gen_inc(TCGv_i32 op)
{
    TCGv_i32 half_op = tcg_temp_new_i32();
    TCGv_i32 ret = tcg_temp_new_i32();

    tcg_gen_extract_i32(half_op, op, 0, 4);

    tcg_gen_addi_i32(ret, op, 1);
    tcg_gen_addi_i32(half_op, half_op, 1);
    tcg_gen_extract_i32(op, ret, 0, 8);

    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_z, op, 0);
    tcg_gen_setcondi_i32(TCG_COND_GEU, cpu_psw_ac, half_op, 0x10);
}

static bool trans_INC_r(DisasContext *ctx, arg_INC_r *a)
{
    TCGv_i32 r = cpu_regs[a->r];
    rl78_gen_inc(r);

    return true;
}

static bool trans_INC_addr(DisasContext *ctx, arg_INC_addr *a)
{
    TCGv_i32 ptr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv_i32 mem = tcg_temp_new_i32();
    
    rl78_gen_lb(ctx, mem, ptr);
    rl78_gen_inc(mem);
    rl78_gen_sb(ctx, mem, ptr);

    return true;
}

static bool trans_INC_indHLoffset(DisasContext *ctx, arg_INC_indHLoffset *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_HL);
    TCGv_i32 ptr = rl78_indirect_ptr(base, tcg_constant_i32(a->offset));
    TCGv_i32 mem = tcg_temp_new_i32();

    rl78_gen_lb(ctx, mem, ptr);
    rl78_gen_inc(mem);
    rl78_gen_sb(ctx, mem, ptr);

    return true;
}

static void rl78_gen_dec(TCGv_i32 op)
{
    TCGv_i32 half_op = tcg_temp_new_i32();
    TCGv_i32 ret = tcg_temp_new_i32();

    tcg_gen_extract_i32(half_op, op, 0, 4);

    tcg_gen_subi_i32(ret, op, 1);
    tcg_gen_extract_i32(op, ret, 0, 8);

    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_psw_z, op, 0);
    tcg_gen_setcondi_i32(TCG_COND_LTU, cpu_psw_ac, half_op, 1);
}

static bool trans_DEC_r(DisasContext *ctx, arg_DEC_r *a)
{
    TCGv_i32 r = cpu_regs[a->r];
    rl78_gen_dec(r);

    return true;
}

static bool trans_DEC_addr(DisasContext *ctx, arg_DEC_addr *a)
{
    TCGv_i32 ptr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv_i32 mem = tcg_temp_new_i32();
    
    rl78_gen_lb(ctx, mem, ptr);
    rl78_gen_dec(mem);
    rl78_gen_sb(ctx, mem, ptr);

    return true;
}

static bool trans_DEC_indHLoffset(DisasContext *ctx, arg_DEC_indHLoffset *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_HL);
    TCGv_i32 ptr = rl78_indirect_ptr(base, tcg_constant_i32(a->offset));
    TCGv_i32 mem = tcg_temp_new_i32();

    rl78_gen_lb(ctx, mem, ptr);
    rl78_gen_dec(mem);
    rl78_gen_sb(ctx, mem, ptr);

    return true;
}

static void rl78_gen_incw(TCGv_i32 op)
{
    tcg_gen_addi_i32(op, op, 1);
    tcg_gen_extract_i32(op, op, 0, 16);
}

static bool trans_INCW_rp(DisasContext *ctx, arg_INCW_rp *a)
{
    TCGv_i32 rp = rl78_load_rp(a->rp * 2);
    rl78_gen_incw(rp);
    rl78_store_rp(a->rp*2, rp);

    return true;
}

static bool trans_INCW_addr(DisasContext *ctx, arg_INCW_addr *a)
{
    TCGv_i32 ptr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv_i32 mem = tcg_temp_new_i32();
    
    rl78_gen_lw(ctx, mem, ptr);
    rl78_gen_incw(mem);
    rl78_gen_sw(ctx, mem, ptr);

    return true;
}

static bool trans_INCW_indHLoffset(DisasContext *ctx, arg_INCW_indHLoffset *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_HL);
    TCGv_i32 ptr = rl78_indirect_ptr(base, tcg_constant_i32(a->offset));
    TCGv_i32 mem = tcg_temp_new_i32();

    rl78_gen_lw(ctx, mem, ptr);
    rl78_gen_incw(mem);
    rl78_gen_sw(ctx, mem, ptr);

    return true;
}

static void rl78_gen_decw(TCGv_i32 op)
{
    tcg_gen_subi_i32(op, op, 1);
    tcg_gen_extract_i32(op, op, 0, 16);
}

static bool trans_DECW_rp(DisasContext *ctx, arg_DECW_rp *a)
{
    TCGv_i32 rp = rl78_load_rp(a->rp * 2);
    rl78_gen_decw(rp);
    rl78_store_rp(a->rp*2, rp);

    return true;
}

static bool trans_DECW_addr(DisasContext *ctx, arg_DECW_addr *a)
{
    TCGv_i32 ptr = rl78_gen_addr(a->adrl, a->adrh, tcg_constant_i32(0x0F));
    TCGv_i32 mem = tcg_temp_new_i32();
    
    rl78_gen_lw(ctx, mem, ptr);
    rl78_gen_decw(mem);
    rl78_gen_sw(ctx, mem, ptr);

    return true;
}

static bool trans_DECW_indHLoffset(DisasContext *ctx, arg_DECW_indHLoffset *a)
{
    TCGv_i32 base = rl78_load_rp(RL78_GPREG_HL);
    TCGv_i32 ptr = rl78_indirect_ptr(base, tcg_constant_i32(a->offset));
    TCGv_i32 mem = tcg_temp_new_i32();

    rl78_gen_lw(ctx, mem, ptr);
    rl78_gen_decw(mem);
    rl78_gen_sw(ctx, mem, ptr);

    return true;
}

static void rl78_gen_shl(TCGv_i32 op, const uint shamt, const uint width)
{
    TCGv_i32 cybit = tcg_temp_new_i32();
    const uint mask = 1 << width;

    tcg_gen_shli_i32(op, op, shamt);

    tcg_gen_andi_i32(cybit, op, mask);
    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_psw_cy, cybit, 0);

    tcg_gen_extract_i32(op, op, 0, width);
}

static bool trans_SHL_A_shamt(DisasContext *ctx, arg_SHL_A_shamt *a)
{
    rl78_gen_shl(cpu_regs[RL78_GPREG_A], a->shamt, 8);
    return true;
}

static bool trans_SHL_B_shamt(DisasContext *ctx, arg_SHL_B_shamt *a)
{
    rl78_gen_shl(cpu_regs[RL78_GPREG_B], a->shamt, 8);
    return true;
}

static bool trans_SHL_C_shamt(DisasContext *ctx, arg_SHL_C_shamt *a)
{
    rl78_gen_shl(cpu_regs[RL78_GPREG_C], a->shamt, 8);
    return true;
}

static bool trans_SHLW_AX_shamt(DisasContext *ctx, arg_SHLW_AX_shamt *a)
{
    TCGv_i32 rp = rl78_load_rp(RL78_GPREG_AX);
    rl78_gen_shl(rp, a->shamt, 16);
    rl78_store_rp(RL78_GPREG_AX, rp);

    return true;
}

static bool trans_SHLW_BC_shamt(DisasContext *ctx, arg_SHLW_BC_shamt *a)
{
    TCGv_i32 rp = rl78_load_rp(RL78_GPREG_BC);
    rl78_gen_shl(rp, a->shamt, 16);
    rl78_store_rp(RL78_GPREG_BC, rp);

    return true;
}

static bool rl78_gen_shr(TCGv_i32 op, const uint shamt, const uint width)
{
    TCGv_i32 cybit = tcg_temp_new_i32();
    const uint tcg_shamt = shamt - 1;

    tcg_gen_shri_i32(op, op, tcg_shamt);

    tcg_gen_andi_i32(cybit, op, 1);
    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_psw_cy, cybit, 0);

    tcg_gen_extract_i32(op, op, 1, width);

    return true;
}

static bool trans_SHR_A_shamt(DisasContext *ctx, arg_SHR_A_shamt *a)
{
    rl78_gen_shr(cpu_regs[RL78_GPREG_A], a->shamt, 8);
    return true;
}

static bool trans_SHRW_AX_shamt(DisasContext *ctx, arg_SHRW_AX_shamt *a)
{
    TCGv_i32 rp = rl78_load_rp(RL78_GPREG_AX);
    rl78_gen_shr(rp, a->shamt, 16);
    rl78_store_rp(RL78_GPREG_AX, rp);

    return true;
}

static bool rl78_gen_sar(TCGv_i32 op, const uint shamt, const uint width)
{
    TCGv_i32 cybit = tcg_temp_new_i32();
    const uint tcg_shamt = shamt - 1;

    tcg_gen_sextract_i32(op, op, 0, width);
    tcg_gen_sari_i32(op, op, tcg_shamt);
    tcg_gen_andi_i32(cybit, op, 1);
    tcg_gen_setcondi_i32(TCG_COND_NE, cpu_psw_cy, cybit, 0);

    tcg_gen_extract_i32(op, op, 1, width);

    return true;
}

static bool trans_SAR_A_shamt(DisasContext *ctx, arg_SAR_A_shamt *a)
{
    rl78_gen_sar(cpu_regs[RL78_GPREG_A], a->shamt, 8);
    return true;
}

static bool trans_SARW_AX_shamt(DisasContext *ctx, arg_SARW_AX_shamt *a)
{
    TCGv_i32 rp = rl78_load_rp(RL78_GPREG_AX);
    rl78_gen_sar(rp, a->shamt, 16);
    rl78_store_rp(RL78_GPREG_AX, rp);

    return true;
}

static bool trans_ROR(DisasContext *ctx, arg_ROR *a)
{
    TCGv_i32 bit = tcg_temp_new_i32();
    TCGv_i32 r = tcg_temp_new_i32();

    tcg_gen_mov_i32(r, cpu_regs[RL78_GPREG_A]);
    tcg_gen_andi_i32(bit, r, 0x01);

    tcg_gen_shri_i32(r, r, 1);
    tcg_gen_deposit_i32(r, r, bit, 7, 1);
    tcg_gen_extract_i32(cpu_regs[RL78_GPREG_A], r, 0, 8);
    tcg_gen_mov_i32(cpu_psw_cy, bit);

    return true;
}

static bool trans_ROL(DisasContext *ctx, arg_ROL *a)
{
    TCGv_i32 bit = tcg_temp_new_i32();
    TCGv_i32 r = tcg_temp_new_i32();

    tcg_gen_mov_i32(r, cpu_regs[RL78_GPREG_A]);
    tcg_gen_setcondi_i32(TCG_COND_GEU, bit, r, 0x80);

    tcg_gen_shli_i32(r, r, 1);
    tcg_gen_deposit_i32(r, r, bit, 0, 1);
    tcg_gen_extract_i32(cpu_regs[RL78_GPREG_A], r, 0, 8);
    tcg_gen_mov_i32(cpu_psw_cy, bit);

    return true;
}

static bool trans_RORC(DisasContext *ctx, arg_RORC *a)
{
    TCGv_i32 lsb = tcg_temp_new_i32();
    TCGv_i32 msb = tcg_temp_new_i32();
    TCGv_i32 r = tcg_temp_new_i32();

    tcg_gen_mov_i32(r, cpu_regs[RL78_GPREG_A]);
    tcg_gen_movcond_i32(TCG_COND_EQ, msb, cpu_psw_cy, tcg_constant_i32(1), tcg_constant_i32(0x80), tcg_constant_i32(0x00));
    tcg_gen_andi_i32(lsb, r, 0x01);

    tcg_gen_shri_i32(r, r, 1);
    tcg_gen_or_i32(r, r, msb);
    tcg_gen_extract_i32(cpu_regs[RL78_GPREG_A], r, 0, 8);
    tcg_gen_mov_i32(cpu_psw_cy, lsb);

    return true;
}

static bool trans_ROLC(DisasContext *ctx, arg_ROLC *a)
{
    TCGv_i32 lsb = tcg_temp_new_i32();
    TCGv_i32 msb = tcg_temp_new_i32();
    TCGv_i32 r = tcg_temp_new_i32();

    tcg_gen_mov_i32(r, cpu_regs[RL78_GPREG_A]);
    tcg_gen_mov_i32(lsb, cpu_psw_cy);
    tcg_gen_setcondi_i32(TCG_COND_GEU, msb, r, 0x80);

    tcg_gen_shli_i32(r, r, 1);
    tcg_gen_or_i32(r, r, lsb);
    tcg_gen_extract_i32(cpu_regs[RL78_GPREG_A], r, 0, 8);
    tcg_gen_mov_i32(cpu_psw_cy, msb);

    return true;
}

static bool trans_ROLWC_rp(RL78GPRegister rp)
{
    TCGv_i32 lsb = tcg_temp_new_i32();
    TCGv_i32 msb = tcg_temp_new_i32();
    TCGv_i32 op = rl78_load_rp(rp);

    tcg_gen_mov_i32(lsb, cpu_psw_cy);
    tcg_gen_setcondi_i32(TCG_COND_GEU, msb, op, 0x8000);

    tcg_gen_shli_i32(op, op, 1);
    tcg_gen_or_i32(op, op, lsb);
    tcg_gen_extract_i32(op, op, 0, 16);
    tcg_gen_mov_i32(cpu_psw_cy, msb);

    rl78_store_rp(rp, op);

    return true;
}

static bool trans_ROLWC_AX(DisasContext *ctx, arg_ROLWC_AX *a)
{
    return trans_ROLWC_rp(RL78_GPREG_AX);
}

static bool trans_ROLWC_BC(DisasContext *ctx, arg_ROLWC_BC *a)
{
    return trans_ROLWC_rp(RL78_GPREG_BC);
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