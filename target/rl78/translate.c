#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "cpu.h"
#include "decode.h"
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
    CPURL78State *env;
    uint32_t pc;
    uint32_t tb_flags;

    bool skip_flag;
    bool use_es;

    MemMapEntry standard_sfr;
    MemMapEntry extended_sfr;
    MemMapEntry mirror;
} DisasContext;

typedef struct RL78BitOperand {
    TCGv_i32 byte;
    TCGv_i32 bit;
} RL78BitData;

enum {
    DISAS_EXIT = DISAS_TARGET_0,
};

#define TB_EXIT_NOBRANCH TB_EXIT_IDX0
#define TB_EXIT_JUMP   TB_EXIT_IDX0
#define TB_EXIT_BRANCH TB_EXIT_IDX1

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

static TCGv_i32 rl78_gen_addr(DisasContext *ctx, TCGv_i32 addr)
{
    TCGv_i32 ret = tcg_temp_new_i32();
    TCGv_i32 es  = tcg_temp_new_i32();

    if (ctx->use_es) {
        tcg_gen_shli_i32(es, cpu_es, 16);
    } else {
        tcg_gen_movi_i32(es, 0xF0000);
    }

    tcg_gen_mov_i32(ret, addr);
    tcg_gen_or_i32(ret, ret, es);

    return ret;
}

static TCGv_i32 rl78_gen_load(DisasContext *ctx, TCGv_i32 addr, MemOp memop)
{
    TCGv_i32 ret = tcg_temp_new_i32();

    TCGv_i32 cond  = tcg_temp_new_i32();
    TCGv_i32 cond0 = tcg_temp_new_i32();
    TCGv_i32 cond1 = tcg_temp_new_i32();

    TCGLabel *as_system  = gen_new_label();
    TCGLabel *as_control = gen_new_label();
    TCGLabel *as_alias   = gen_new_label();
    TCGLabel *end        = gen_new_label();

    TCGLabel *labels[] = {
        [RL78_AS_SYSTEM]  = as_system,
        [RL78_AS_CONTROL] = as_control,
        [RL78_AS_ALIAS]   = as_alias,
    };

    TCGLabel *mm_label_table[] = {
        as_control,
        as_control,
        as_alias,
    };

    MemMapEntry maps[] = {
        ctx->standard_sfr,
        ctx->extended_sfr,
        ctx->mirror,
    };

    for (int i = 0; i < ARRAY_SIZE(mm_label_table); i++) {
        tcg_gen_setcondi_i32(TCG_COND_GE, cond0, addr, maps[i].base);
        tcg_gen_setcondi_i32(TCG_COND_LT, cond1, addr,
                             maps[i].base + maps[i].size);
        tcg_gen_and_i32(cond, cond0, cond1);
        tcg_gen_brcondi_i32(TCG_COND_EQ, cond, 1, mm_label_table[i]);
    }

    for (int i = 0; i < ARRAY_SIZE(labels); i++) {
        gen_set_label(labels[i]);
        tcg_gen_qemu_ld_i32(ret, addr, (TCGArg)i, memop);
        tcg_gen_br(end);
    }

    gen_set_label(end);

    return ret;
}

static void rl78_gen_store(DisasContext *ctx, TCGv_i32 addr, TCGv_i32 data,
                           MemOp memop)
{
    TCGv_i32 cond  = tcg_temp_new_i32();
    TCGv_i32 cond0 = tcg_temp_new_i32();
    TCGv_i32 cond1 = tcg_temp_new_i32();

    TCGLabel *as_system  = gen_new_label();
    TCGLabel *as_control = gen_new_label();
    TCGLabel *as_alias   = gen_new_label();
    TCGLabel *end        = gen_new_label();

    TCGLabel *labels[] = {
        [RL78_AS_SYSTEM]  = as_system,
        [RL78_AS_CONTROL] = as_control,
        [RL78_AS_ALIAS]   = as_alias,
    };

    TCGLabel *mm_label_table[] = {
        as_control,
        as_control,
        as_alias,
    };

    MemMapEntry maps[] = {
        ctx->standard_sfr,
        ctx->extended_sfr,
        ctx->mirror,
    };

    for (int i = 0; i < ARRAY_SIZE(mm_label_table); i++) {
        tcg_gen_setcondi_i32(TCG_COND_GE, cond0, addr, maps[i].base);
        tcg_gen_setcondi_i32(TCG_COND_LT, cond1, addr,
                             maps[i].base + maps[i].size);
        tcg_gen_and_i32(cond, cond0, cond1);
        tcg_gen_brcondi_i32(TCG_COND_EQ, cond, 1, mm_label_table[i]);
    }

    for (int i = 0; i < ARRAY_SIZE(labels); i++) {
        gen_set_label(labels[i]);
        tcg_gen_qemu_st_i32(data, addr, (TCGArg)i, memop);
        tcg_gen_br(end);
    }

    gen_set_label(end);
}

static TCGv_i32 rl78_gen_lb(DisasContext *ctx, TCGv_i32 addr)
{
    return rl78_gen_load(ctx, addr, MO_8);
}

static TCGv_i32 rl78_gen_lw(DisasContext *ctx, TCGv_i32 addr)
{
    TCGv_i32 access_addr = tcg_temp_new_i32();

    tcg_gen_andi_i32(access_addr, addr, ~0x01);

    return rl78_gen_load(ctx, access_addr, MO_16);
}

static void rl78_gen_sb(DisasContext *ctx, TCGv_i32 addr, TCGv_i32 data)
{
    rl78_gen_store(ctx, addr, data, MO_8);
}

static void rl78_gen_sw(DisasContext *ctx, TCGv_i32 addr, TCGv_i32 data)
{
    TCGv_i32 access_addr = tcg_temp_new_i32();

    tcg_gen_andi_i32(access_addr, addr, ~0x01);

    rl78_gen_store(ctx, access_addr, data, MO_16);
}

static RL78AddressSpace rl78_gen_mmuidx_static(DisasContext *ctx, vaddr addr)
{
    const MemMapEntry maps[] = {
        ctx->mirror,
        ctx->standard_sfr,
        ctx->extended_sfr,
    };

    const RL78AddressSpace mmuidx[] = {
        RL78_AS_ALIAS,
        RL78_AS_CONTROL,
        RL78_AS_CONTROL,
    };

    for (int i = 0; i < ARRAY_SIZE(maps); i++) {
        if (maps[i].base <= addr && addr < maps[i].base + maps[i].size) {
            return mmuidx[i];
        }
    }

    return RL78_AS_SYSTEM;
}

static TCGv_i32 rl78_gen_lb_static(DisasContext *ctx, vaddr addr)
{
    const RL78AddressSpace mmuidx = rl78_gen_mmuidx_static(ctx, addr);
    TCGv_i32 ret                  = tcg_temp_new_i32();
    TCGv_i32 adr                  = tcg_constant_i32(addr);

    tcg_gen_qemu_ld_i32(ret, adr, (TCGArg)mmuidx, MO_8);
    return ret;
}

static TCGv_i32 rl78_gen_lw_static(DisasContext *ctx, vaddr addr)
{
    const RL78AddressSpace mmuidx = rl78_gen_mmuidx_static(ctx, addr);
    TCGv_i32 ret                  = tcg_temp_new_i32();
    TCGv_i32 adr                  = tcg_constant_i32(addr & ~0x01);

    tcg_gen_qemu_ld_i32(ret, adr, (TCGArg)mmuidx, MO_16);
    return ret;
}

static void rl78_gen_sb_static(DisasContext *ctx, vaddr addr, TCGv_i32 data)
{
    const RL78AddressSpace mmuidx = rl78_gen_mmuidx_static(ctx, addr);
    TCGv_i32 adr                  = tcg_constant_i32(addr);

    tcg_gen_qemu_st_i32(data, adr, (TCGArg)mmuidx, MO_8);
    return;
}

static void rl78_gen_sw_static(DisasContext *ctx, vaddr addr, TCGv_i32 data)
{
    const RL78AddressSpace mmuidx = rl78_gen_mmuidx_static(ctx, addr);
    TCGv_i32 adr                  = tcg_constant_i32(addr & ~0x01);

    tcg_gen_qemu_st_i32(data, adr, (TCGArg)mmuidx, MO_16);
    return;
}

static TCGv_i32 rl78_gen_load_static(DisasContext *ctx, vaddr addr, MemOp memop)
{
    switch (memop & MO_SIZE) {
    case MO_8:
        return rl78_gen_lb_static(ctx, addr);
    case MO_16:
        return rl78_gen_lw_static(ctx, addr);
    default:
        // implementation bug assertion
        return tcg_constant_i32(0);
    }
}

static void rl78_gen_store_static(DisasContext *ctx, vaddr addr, TCGv_i32 data,
                                  MemOp memop)
{
    switch (memop & MO_SIZE) {
    case MO_8:
        rl78_gen_sb_static(ctx, addr, data);
        break;
    case MO_16:
        rl78_gen_sw_static(ctx, addr, data);
        break;
    default:
        // implementation bug assertion
        break;
    }
}

static TCGv_i32 byte_reg_addr(const RL78ByteRegister reg)
{
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_mov_i32(addr, cpu_psw_rbs);
    tcg_gen_shli_i32(addr, addr, 3);
    tcg_gen_addi_i32(addr, addr, reg);
    tcg_gen_addi_i32(addr, addr, 0xFFEE0);
    return addr;
}

static TCGv_i32 word_reg_addr(const RL78WordRegister reg)
{
    static const uint32_t offsets[] = {
        0x0000,
        0x0002,
        0x0004,
        0x0006,
    };
    TCGv_i32 addr = tcg_temp_new_i32();
    tcg_gen_mov_i32(addr, cpu_psw_rbs);
    tcg_gen_shli_i32(addr, addr, 3);
    tcg_gen_addi_i32(addr, addr, offsets[reg]);
    tcg_gen_addi_i32(addr, addr, 0xFFEE0);

    return addr;
}

static TCGv_i32 load_byte_reg(const RL78ByteRegister reg)
{
    TCGv_i32 ret  = tcg_temp_new_i32();
    TCGv_i32 addr = byte_reg_addr(reg);

    tcg_gen_qemu_ld_i32(ret, addr, (TCGArg)RL78_AS_SYSTEM, MO_8);

    return ret;
}

static TCGv_i32 load_word_reg(const RL78WordRegister reg)
{
    TCGv_i32 ret  = tcg_temp_new_i32();
    TCGv_i32 addr = word_reg_addr(reg);

    tcg_gen_qemu_ld_i32(ret, addr, (TCGArg)RL78_AS_SYSTEM, MO_16);

    return ret;
}

static void store_byte_reg(const RL78ByteRegister reg, TCGv_i32 data)
{
    TCGv_i32 addr = byte_reg_addr(reg);
    tcg_gen_qemu_st_i32(data, addr, (TCGArg)RL78_AS_SYSTEM, MO_8);
}

static void store_word_reg(const RL78WordRegister reg, TCGv_i32 data)
{
    TCGv_i32 addr = word_reg_addr(reg);
    tcg_gen_qemu_st_i32(data, addr, (TCGArg)RL78_AS_SYSTEM, MO_16);
}

static TCGv_i32 load_psw(void)
{
    TCGv_i32 ret  = tcg_temp_new_i32();
    TCGv_i32 rbs0 = tcg_temp_new_i32();
    TCGv_i32 rbs1 = tcg_temp_new_i32();

    tcg_gen_extract_i32(rbs0, cpu_psw_rbs, 0, 1);
    tcg_gen_extract_i32(rbs1, cpu_psw_rbs, 1, 1);

    tcg_gen_mov_i32(ret, cpu_psw_cy);
    tcg_gen_deposit_i32(ret, ret, cpu_psw_isp, 1, 2);
    tcg_gen_deposit_i32(ret, ret, rbs0, 3, 1);
    tcg_gen_deposit_i32(ret, ret, cpu_psw_ac, 4, 1);
    tcg_gen_deposit_i32(ret, ret, rbs1, 5, 1);
    tcg_gen_deposit_i32(ret, ret, cpu_psw_z, 6, 1);
    tcg_gen_deposit_i32(ret, ret, cpu_psw_ie, 7, 1);

    return ret;
}

static void store_psw(TCGv_i32 psw)
{
    TCGv_i32 rbs0 = tcg_temp_new_i32();
    TCGv_i32 rbs1 = tcg_temp_new_i32();

    tcg_gen_extract_i32(cpu_psw_cy, psw, 0, 1);
    tcg_gen_extract_i32(cpu_psw_isp, psw, 1, 2);
    tcg_gen_extract_i32(cpu_psw_ac, psw, 4, 1);
    tcg_gen_extract_i32(cpu_psw_z, psw, 6, 1);
    tcg_gen_extract_i32(cpu_psw_ie, psw, 7, 1);

    tcg_gen_movi_i32(cpu_psw_rbs, 0);
    tcg_gen_extract_i32(rbs0, psw, 3, 1);
    tcg_gen_extract_i32(rbs1, psw, 5, 1);
    tcg_gen_deposit_i32(cpu_psw_rbs, cpu_psw_rbs, rbs0, 0, 1);
    tcg_gen_deposit_i32(cpu_psw_rbs, cpu_psw_rbs, rbs1, 1, 1);
}

static TCGv_i32 load_abs16(DisasContext *ctx, const uint32_t addr,
                           const MemOp memop)
{
    TCGv_i32 a = rl78_gen_addr(ctx, tcg_constant_i32(addr));
    return rl78_gen_load(ctx, a, memop);
}

static void store_abs16(DisasContext *ctx, const uint32_t addr, TCGv_i32 data,
                        const MemOp memop)
{
    TCGv_i32 a = rl78_gen_addr(ctx, tcg_constant_i32(addr));
    rl78_gen_store(ctx, a, data, memop);
}

static TCGv_i32 load_saddr(DisasContext *ctx, const uint32_t saddr,
                           const MemOp memop)
{
    const vaddr base        = saddr < 0x20 ? 0xFFF00 : 0xFFE00;
    const vaddr access_addr = base + saddr;

    return rl78_gen_load_static(ctx, access_addr, memop);
}

static void store_saddr(DisasContext *ctx, const uint32_t saddr, TCGv_i32 data,
                        const MemOp memop)
{
    const vaddr base        = saddr < 0x20 ? 0xFFF00 : 0xFFE00;
    const vaddr access_addr = base + saddr;

    rl78_gen_store_static(ctx, access_addr, data, memop);
}

static TCGv_i32 load_sfr(DisasContext *ctx, const uint32_t sfr,
                         const MemOp memop)
{
    const vaddr access_addr = 0xFFF00 + sfr;
    return rl78_gen_load_static(ctx, access_addr, memop);
}

static void store_sfr(DisasContext *ctx, const uint32_t sfr, TCGv_i32 data,
                      const MemOp memop)
{
    const vaddr access_addr = 0xFFF00 + sfr;
    rl78_gen_store_static(ctx, access_addr, data, memop);
}

static TCGv_i32 ind_reg_reg(DisasContext *ctx, const RL78WordRegister base,
                            const RL78ByteRegister idx)
{
    TCGv_i32 tcg_base = load_word_reg(base);
    TCGv_i32 tcg_idx  = load_byte_reg(idx);
    tcg_gen_add_i32(tcg_base, tcg_base, tcg_idx);

    return rl78_gen_addr(ctx, tcg_base);
}

static TCGv_i32 load_ind_reg_reg(DisasContext *ctx,
                                 const RL78OperandIndRegReg op,
                                 const MemOp memop)
{
    TCGv_i32 addr = ind_reg_reg(ctx, op.base, op.idx);

    return rl78_gen_load(ctx, addr, memop);
}

static void store_ind_reg_reg(DisasContext *ctx, const RL78OperandIndRegReg op,
                              TCGv_i32 data, const MemOp memop)
{
    TCGv_i32 addr = ind_reg_reg(ctx, op.base, op.idx);

    rl78_gen_store(ctx, addr, data, memop);
}

static TCGv_i32 ind_reg_imm(DisasContext *ctx, const RL78WordRegister base,
                            const uint32_t imm)
{
    TCGv_i32 tcg_base = load_word_reg(base);
    tcg_gen_addi_i32(tcg_base, tcg_base, imm);

    return rl78_gen_addr(ctx, tcg_base);
}

static TCGv_i32 load_ind_reg_imm(DisasContext *ctx,
                                 const RL78OperandIndRegImm op,
                                 const MemOp memop)
{
    const TCGv_i32 addr = ind_reg_imm(ctx, op.base, op.imm);
    return rl78_gen_load(ctx, addr, memop);
}

static void store_ind_reg_imm(DisasContext *ctx, const RL78OperandIndRegImm op,
                              TCGv_i32 data, const MemOp memop)
{
    const TCGv_i32 addr = ind_reg_imm(ctx, op.base, op.imm);
    rl78_gen_store(ctx, addr, data, memop);
}

static TCGv_i32 ind_base_byte(DisasContext *ctx, const uint32_t base,
                              const RL78ByteRegister idx)
{
    TCGv_i32 tcg_base = tcg_constant_i32(base);
    TCGv_i32 tcg_idx  = load_byte_reg(idx);
    tcg_gen_add_i32(tcg_base, tcg_base, tcg_idx);

    return rl78_gen_addr(ctx, tcg_base);
}

static TCGv_i32 load_ind_base_byte(DisasContext *ctx,
                                   const RL78OperandIndBaseByte op,
                                   const MemOp memop)
{
    const TCGv_i32 addr = ind_base_byte(ctx, op.base, op.idx);
    return rl78_gen_load(ctx, addr, memop);
}

static void store_ind_base_byte(DisasContext *ctx,
                                const RL78OperandIndBaseByte op, TCGv_i32 data,
                                const MemOp memop)
{
    const TCGv_i32 addr = ind_base_byte(ctx, op.base, op.idx);
    rl78_gen_store(ctx, addr, data, memop);
}

static TCGv_i32 ind_base_word(DisasContext *ctx, const uint32_t base,
                              const RL78WordRegister idx)
{
    TCGv_i32 tcg_base = tcg_constant_i32(base);
    TCGv_i32 tcg_idx  = load_word_reg(idx);
    tcg_gen_add_i32(tcg_base, tcg_base, tcg_idx);

    return rl78_gen_addr(ctx, tcg_base);
}

static TCGv_i32 load_ind_base_word(DisasContext *ctx,
                                   const RL78OperandIndBaseWord op,
                                   const MemOp memop)
{
    const TCGv_i32 addr = ind_base_word(ctx, op.base, op.idx);
    return rl78_gen_load(ctx, addr, memop);
}

static void store_ind_base_word(DisasContext *ctx,
                                const RL78OperandIndBaseWord op, TCGv_i32 data,
                                const MemOp memop)
{
    const TCGv_i32 addr = ind_base_word(ctx, op.base, op.idx);
    rl78_gen_store(ctx, addr, data, memop);
}

#define control_reg(reg)                        \
    static TCGv_i32 load_##reg(void) {          \
        TCGv_i32 ret = tcg_temp_new_i32();      \
        tcg_gen_mov_i32(ret, cpu_##reg);        \
        return ret;                             \
    }                                           \
                                                \
    static void store_##reg(TCGv_i32 data) {    \
        tcg_gen_mov_i32(cpu_##reg, data);       \
    }

control_reg(sp);
control_reg(es);
control_reg(psw_cy);

static TCGv_i32 rl78_gen_load_operand(DisasContext *ctx, const RL78Operand op)
{
    switch (op.kind) {
    case RL78_OP_BYTE_REG:
        return load_byte_reg(op.byte_reg);
    case RL78_OP_WORD_REG:
        return load_word_reg(op.word_reg);
    case RL78_OP_PSW:
        return load_psw();
    case RL78_OP_SP:
        return load_sp();
    case RL78_OP_ES:
        return load_es();
    case RL78_OP_CY:
        return load_psw_cy();
    case RL78_OP_IMM8:
        return tcg_constant_i32(op.const_op & 0xFF);
    case RL78_OP_IMM16:
        return tcg_constant_i32(op.const_op & 0xFFFF);
    case RL78_OP_ABS16:
        return load_abs16(ctx, op.const_op, op.memop);
    case RL78_OP_SADDR:
        return load_saddr(ctx, op.const_op, op.memop);
    case RL78_OP_SFR:
        return load_sfr(ctx, op.const_op, op.memop);
    case RL78_OP_IND_REG_REG:
        return load_ind_reg_reg(ctx, op.ind_reg_reg, op.memop);
    case RL78_OP_IND_REG_IMM:
        return load_ind_reg_imm(ctx, op.ind_reg_imm, op.memop);
    case RL78_OP_IND_BASE_BYTE:
        return load_ind_base_byte(ctx, op.ind_base_byte, op.memop);
    case RL78_OP_IND_BASE_WORD:
        return load_ind_base_word(ctx, op.ind_base_word, op.memop);
    case RL78_OP_SHAMT:
        return tcg_constant_i32(op.const_op & 0x1F);
    case RL78_OP_SEL_RB:
        return tcg_constant_i32(op.const_op & 0x03);
    case RL78_OP_BIT:
    case RL78_OP_ABS20:
    case RL78_OP_REL8:
    case RL78_OP_REL16:
    case RL78_OP_CALLT:
    case RL78_OP_NONE:
    default:
        // implementation bug assertion
        break;
    }

    return tcg_constant_i32(0);
}

static void rl78_gen_store_operand(DisasContext *ctx, const RL78Operand op,
                                   TCGv_i32 data)
{
    switch (op.kind) {
    case RL78_OP_BYTE_REG:
        store_byte_reg(op.byte_reg, data);
        break;
    case RL78_OP_WORD_REG:
        store_word_reg(op.word_reg, data);
        break;
    case RL78_OP_PSW:
        store_psw(data);
        break;
    case RL78_OP_SP:
        store_sp(data);
        break;
    case RL78_OP_ES:
        store_es(data);
        break;
    case RL78_OP_CY:
        store_psw_cy(data);
        break;
    case RL78_OP_ABS16:
        store_abs16(ctx, op.const_op, data, op.memop);
        break;
    case RL78_OP_SADDR:
        store_saddr(ctx, op.const_op, data, op.memop);
        break;
    case RL78_OP_SFR:
        store_sfr(ctx, op.const_op, data, op.memop);
        break;
    case RL78_OP_IND_REG_REG:
        store_ind_reg_reg(ctx, op.ind_reg_reg, data, op.memop);
        break;
    case RL78_OP_IND_REG_IMM:
        store_ind_reg_imm(ctx, op.ind_reg_imm, data, op.memop);
        break;
    case RL78_OP_IND_BASE_BYTE:
        store_ind_base_byte(ctx, op.ind_base_byte, data, op.memop);
        break;
    case RL78_OP_IND_BASE_WORD:
        store_ind_base_word(ctx, op.ind_base_word, data, op.memop);
        break;
    case RL78_OP_SHAMT:
    case RL78_OP_SEL_RB:
    case RL78_OP_IMM8:
    case RL78_OP_IMM16:
    case RL78_OP_BIT:
    case RL78_OP_ABS20:
    case RL78_OP_REL8:
    case RL78_OP_REL16:
    case RL78_OP_CALLT:
    case RL78_OP_NONE:
    default:
        // implementation bug assertion
        break;
    }
}

static RL78BitData rl78_gen_load_bit(DisasContext *ctx, const RL78OperandBit op)
{
    TCGv_i32 data;
    TCGv_i32 bit = tcg_temp_new_i32();

    switch (op.kind) {
    case RL78_BITOP_SADDR:
        data = load_saddr(ctx, op.addr, MO_8);
        break;
    case RL78_BITOP_SFR:
        data = load_sfr(ctx, op.addr, MO_8);
        break;
    case RL78_BITOP_REG_A:
        data = load_byte_reg(RL78_BYTE_REG_A);
        break;
    case RL78_BITOP_ABS16:
        data = load_abs16(ctx, op.addr, MO_8);
        break;
    case RL78_BITOP_IND_HL: {
        RL78OperandIndRegImm indop = {.base = RL78_WORD_REG_HL, .imm = 0};

        data = load_ind_reg_imm(ctx, indop, MO_8);
        break;
    }
    default:
        // implementation bug assertion
        break;
    }

    tcg_gen_shli_i32(bit, data, op.bit);
    tcg_gen_andi_i32(bit, bit, 0x01);

    return (RL78BitData){.byte = data, .bit = bit};
}

static void rl78_gen_store_bit(DisasContext *ctx, const RL78OperandBit op,
                               RL78BitData bit)
{
    TCGv_i32 stored_data = tcg_temp_new_i32();
    TCGv_i32 stored_bit  = tcg_temp_new_i32();
    const uint32_t mask  = 1 << op.bit;

    tcg_gen_mov_i32(stored_data, bit.byte);
    tcg_gen_andi_i32(stored_data, stored_data, ~mask);
    tcg_gen_shli_i32(stored_bit, bit.bit, op.bit);
    tcg_gen_or_i32(stored_data, stored_data, stored_bit);

    switch (op.kind) {
    case RL78_BITOP_SADDR:
        rl78_gen_store_static(ctx, op.addr, stored_data, MO_8);
        break;
    case RL78_BITOP_SFR:
        rl78_gen_store_static(ctx, op.addr, stored_data, MO_8);
        break;
    case RL78_BITOP_REG_A:
        store_byte_reg(RL78_BYTE_REG_A, stored_data);
        break;
    case RL78_BITOP_ABS16:
        store_abs16(ctx, op.addr, stored_data, MO_8);
        break;
    case RL78_BITOP_IND_HL: {
        RL78OperandIndRegImm addr = {.base = RL78_WORD_REG_HL, .imm = 0};
        store_ind_reg_imm(ctx, addr, stored_data, MO_8);
        break;
    }
    default:
        // implementation bug assertion
        break;
    }
}

static void rl78_gen_goto_tb(DisasContext *dc, unsigned tb_slot_idx, vaddr dest)
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

static bool trans_MOV(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 src = rl78_gen_load_operand(ctx, insn->operand[1]);
    rl78_gen_store_operand(ctx, insn->operand[0], src);
    return true;
}

static bool trans_XCH(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 op0 = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 op1 = rl78_gen_load_operand(ctx, insn->operand[1]);
    rl78_gen_store_operand(ctx, insn->operand[1], op0);
    rl78_gen_store_operand(ctx, insn->operand[0], op1);
    return true;
}

static bool trans_ONEB(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 op = tcg_constant_i32(1);
    rl78_gen_store_operand(ctx, insn->operand[0], op);
    return true;
}

static bool trans_CLRB(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 op = tcg_constant_i32(0);
    rl78_gen_store_operand(ctx, insn->operand[0], op);
    return true;
}

static bool trans_MOVS(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 is_src_zero = tcg_temp_new_i32();
    TCGv_i32 is_a_zero   = tcg_temp_new_i32();
    TCGv_i32 a           = load_byte_reg(RL78_BYTE_REG_A);
    TCGv_i32 ac          = tcg_temp_new_i32();

    TCGv_i32 src = rl78_gen_load_operand(ctx, insn->operand[1]);
    rl78_gen_store_operand(ctx, insn->operand[0], src);

    tcg_gen_movcond_i32(TCG_COND_EQ, is_src_zero, src, tcg_constant_i32(0),
                        tcg_constant_i32(1), tcg_constant_i32(0));
    tcg_gen_movcond_i32(TCG_COND_EQ, is_a_zero, a, tcg_constant_i32(0),
                        tcg_constant_i32(1), tcg_constant_i32(0));
    tcg_gen_mov_i32(cpu_psw_z, is_src_zero);

    tcg_gen_or_i32(ac, is_src_zero, is_a_zero);
    tcg_gen_mov_i32(cpu_psw_ac, ac);

    return true;
}

static bool trans_MOVW(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 src = rl78_gen_load_operand(ctx, insn->operand[1]);
    rl78_gen_store_operand(ctx, insn->operand[0], src);
    return true;
}

static bool trans_XCHW(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 op0 = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 op1 = rl78_gen_load_operand(ctx, insn->operand[1]);
    rl78_gen_store_operand(ctx, insn->operand[1], op0);
    rl78_gen_store_operand(ctx, insn->operand[0], op1);
    return true;
}

static bool trans_ONEW(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 op = tcg_constant_i32(1);
    rl78_gen_store_operand(ctx, insn->operand[0], op);
    return true;
}

static bool trans_CLRW(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 op = tcg_constant_i32(0);
    rl78_gen_store_operand(ctx, insn->operand[0], op);
    return true;
}

static TCGv_i32 carry_byte(TCGv_i32 result)
{
    TCGv_i32 cy = tcg_temp_new_i32();

    tcg_gen_shri_i32(cy, result, 8);
    tcg_gen_andi_i32(cy, cy, 0x01);

    return cy;
}

static TCGv_i32 borrow_byte(TCGv_i32 result)
{
    TCGv_i32 cy = tcg_temp_new_i32();

    tcg_gen_shri_i32(cy, result, 8);
    tcg_gen_xori_i32(cy, cy, 0x01);
    tcg_gen_andi_i32(cy, cy, 0x01);

    return cy;
}

static TCGv_i32 zero_byte(TCGv_i32 result)
{
    TCGv_i32 z = tcg_temp_new_i32();

    tcg_gen_andi_i32(z, result, 0xFF);
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_psw_z, z, tcg_constant_i32(0),
                        tcg_constant_i32(1), tcg_constant_i32(0));

    return z;
}

static TCGv_i32 carry_word(TCGv_i32 result)
{
    TCGv_i32 cy = tcg_temp_new_i32();

    tcg_gen_shri_i32(cy, result, 16);
    tcg_gen_andi_i32(cy, cy, 0x01);

    return cy;
}

static TCGv_i32 borrow_word(TCGv_i32 result)
{
    TCGv_i32 cy = tcg_temp_new_i32();

    tcg_gen_shri_i32(cy, result, 16);
    tcg_gen_xori_i32(cy, cy, 0x01);
    tcg_gen_andi_i32(cy, cy, 0x01);

    return cy;
}

static TCGv_i32 zero_word(TCGv_i32 result)
{
    TCGv_i32 z = tcg_temp_new_i32();

    tcg_gen_andi_i32(z, result, 0xFFFF);
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_psw_z, z, tcg_constant_i32(0),
                        tcg_constant_i32(1), tcg_constant_i32(0));

    return z;
}

static TCGv_i32 half_carry(TCGv_i32 op0, TCGv_i32 op1, TCGv_i32 result)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    TCGv_i32 ac  = tcg_temp_new_i32();

    tcg_gen_mov_i32(tmp, op0);
    tcg_gen_xor_i32(tmp, tmp, op1);
    tcg_gen_xor_i32(tmp, tmp, result);

    tcg_gen_shri_i32(ac, tmp, 4);
    tcg_gen_andi_i32(ac, ac, 0x01);

    return ac;
}

static TCGv_i32 half_borrow(TCGv_i32 op0, TCGv_i32 op1, TCGv_i32 result)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    TCGv_i32 ac  = tcg_temp_new_i32();

    tcg_gen_mov_i32(tmp, op0);
    tcg_gen_xor_i32(tmp, tmp, op1);
    tcg_gen_xor_i32(tmp, tmp, result);

    tcg_gen_shri_i32(ac, tmp, 4);
    tcg_gen_xori_i32(ac, ac, 0x01);
    tcg_gen_andi_i32(ac, ac, 0x01);

    return ac;
}

static bool trans_ADD(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 op0    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 op1    = rl78_gen_load_operand(ctx, insn->operand[1]);
    TCGv_i32 result = tcg_temp_new_i32();

    tcg_gen_add_i32(result, op0, op1);

    rl78_gen_store_operand(ctx, insn->operand[0], result);

    tcg_gen_mov_i32(cpu_psw_cy, carry_byte(result));
    tcg_gen_mov_i32(cpu_psw_ac, half_carry(op0, op1, result));
    tcg_gen_mov_i32(cpu_psw_z, zero_byte(result));

    return true;
}

static bool trans_ADDC(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 op0    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 op1    = rl78_gen_load_operand(ctx, insn->operand[1]);
    TCGv_i32 result = tcg_temp_new_i32();

    tcg_gen_add_i32(result, op0, op1);
    tcg_gen_add_i32(result, result, cpu_psw_cy);

    rl78_gen_store_operand(ctx, insn->operand[0], result);

    tcg_gen_mov_i32(cpu_psw_cy, carry_byte(result));
    tcg_gen_mov_i32(cpu_psw_ac, half_carry(op0, op1, result));
    tcg_gen_mov_i32(cpu_psw_z, zero_byte(result));

    return true;
}

static bool trans_SUB(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 op0    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 op1    = rl78_gen_load_operand(ctx, insn->operand[1]);
    TCGv_i32 result = tcg_temp_new_i32();

    tcg_gen_sub_i32(result, op0, op1);

    rl78_gen_store_operand(ctx, insn->operand[0], result);

    tcg_gen_mov_i32(cpu_psw_cy, borrow_byte(result));
    tcg_gen_mov_i32(cpu_psw_ac, half_borrow(op0, op1, result));
    tcg_gen_mov_i32(cpu_psw_z, zero_byte(result));

    return true;
}

static bool trans_SUBC(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 op0    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 op1    = rl78_gen_load_operand(ctx, insn->operand[1]);
    TCGv_i32 result = tcg_temp_new_i32();

    tcg_gen_sub_i32(result, op0, op1);
    tcg_gen_sub_i32(result, result, cpu_psw_cy);

    rl78_gen_store_operand(ctx, insn->operand[0], result);

    tcg_gen_mov_i32(cpu_psw_cy, borrow_byte(result));
    tcg_gen_mov_i32(cpu_psw_ac, half_borrow(op0, op1, result));
    tcg_gen_mov_i32(cpu_psw_z, zero_byte(result));

    return true;
}

static bool trans_CMP(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 op0    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 op1    = rl78_gen_load_operand(ctx, insn->operand[1]);
    TCGv_i32 result = tcg_temp_new_i32();

    tcg_gen_sub_i32(result, op0, op1);

    tcg_gen_mov_i32(cpu_psw_cy, borrow_byte(result));
    tcg_gen_mov_i32(cpu_psw_ac, half_borrow(op0, op1, result));
    tcg_gen_mov_i32(cpu_psw_z, zero_byte(result));

    return true;
}

static bool trans_CMP0(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 op0    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 zero   = tcg_constant_i32(0);
    TCGv_i32 result = tcg_temp_new_i32();

    tcg_gen_sub_i32(result, op0, zero);

    tcg_gen_mov_i32(cpu_psw_cy, borrow_byte(result));
    tcg_gen_mov_i32(cpu_psw_ac, half_borrow(op0, zero, result));
    tcg_gen_mov_i32(cpu_psw_z, zero_byte(result));

    return true;
}

static bool bitwise(DisasContext *ctx, RL78Operand dst, RL78Operand src,
                    void (*op)(TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 op0    = rl78_gen_load_operand(ctx, dst);
    TCGv_i32 op1    = rl78_gen_load_operand(ctx, src);
    TCGv_i32 result = tcg_temp_new_i32();

    op(result, op0, op1);
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_psw_z, result, tcg_constant_i32(0),
                        tcg_constant_i32(1), tcg_constant_i32(0));

    rl78_gen_store_operand(ctx, dst, result);

    return true;
}

static bool trans_AND(DisasContext *ctx, RL78Instruction *insn)
{
    return bitwise(ctx, insn->operand[0], insn->operand[1], tcg_gen_and_i32);
}

static bool trans_OR(DisasContext *ctx, RL78Instruction *insn)
{
    return bitwise(ctx, insn->operand[0], insn->operand[1], tcg_gen_or_i32);
}

static bool trans_XOR(DisasContext *ctx, RL78Instruction *insn)
{
    return bitwise(ctx, insn->operand[0], insn->operand[1], tcg_gen_xor_i32);
}

static bool trans_CMPS(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 op0    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 op1    = rl78_gen_load_operand(ctx, insn->operand[1]);
    TCGv_i32 a      = load_byte_reg(RL78_BYTE_REG_A);
    TCGv_i32 result = tcg_temp_new_i32();

    tcg_gen_sub_i32(result, op0, op1);

    tcg_gen_mov_i32(cpu_psw_ac, half_borrow(op0, op1, result));
    tcg_gen_mov_i32(cpu_psw_z, zero_byte(result));

    TCGv_i32 is_result_zero = tcg_temp_new_i32();
    TCGv_i32 is_a_zero      = tcg_temp_new_i32();
    TCGv_i32 is_dst_zero    = tcg_temp_new_i32();

    tcg_gen_andi_i32(result, result, 0xFF);
    tcg_gen_movcond_i32(TCG_COND_EQ, is_result_zero, result,
                        tcg_constant_i32(0), tcg_constant_i32(1),
                        tcg_constant_i32(0));
    tcg_gen_movcond_i32(TCG_COND_EQ, is_a_zero, a, tcg_constant_i32(0),
                        tcg_constant_i32(1), tcg_constant_i32(0));
    tcg_gen_movcond_i32(TCG_COND_EQ, is_dst_zero, op0, tcg_constant_i32(0),
                        tcg_constant_i32(1), tcg_constant_i32(0));

    tcg_gen_or_i32(is_result_zero, is_result_zero, is_a_zero);
    tcg_gen_or_i32(is_result_zero, is_result_zero, is_dst_zero);

    tcg_gen_mov_i32(cpu_psw_cy, is_result_zero);

    return true;
}

static bool trans_ADDW(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 op0    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 op1    = rl78_gen_load_operand(ctx, insn->operand[1]);
    TCGv_i32 result = tcg_temp_new_i32();

    tcg_gen_add_i32(result, op0, op1);

    rl78_gen_store_operand(ctx, insn->operand[0], result);

    tcg_gen_mov_i32(cpu_psw_cy, carry_word(result));
    tcg_gen_mov_i32(cpu_psw_ac, tcg_constant_i32(0));
    tcg_gen_mov_i32(cpu_psw_z, zero_word(result));

    return true;
}

static bool trans_SUBW(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 op0    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 op1    = rl78_gen_load_operand(ctx, insn->operand[1]);
    TCGv_i32 result = tcg_temp_new_i32();

    tcg_gen_sub_i32(result, op0, op1);

    rl78_gen_store_operand(ctx, insn->operand[0], result);

    tcg_gen_mov_i32(cpu_psw_cy, borrow_word(result));
    tcg_gen_mov_i32(cpu_psw_ac, tcg_constant_i32(0));
    tcg_gen_mov_i32(cpu_psw_z, zero_word(result));

    return true;
}

static bool trans_CMPW(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 op0    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 op1    = rl78_gen_load_operand(ctx, insn->operand[1]);
    TCGv_i32 result = tcg_temp_new_i32();

    tcg_gen_sub_i32(result, op0, op1);

    tcg_gen_mov_i32(cpu_psw_cy, borrow_word(result));
    tcg_gen_mov_i32(cpu_psw_ac, tcg_constant_i32(0));
    tcg_gen_mov_i32(cpu_psw_z, zero_word(result));

    return true;
}

static bool trans_MULU(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 a      = load_byte_reg(RL78_BYTE_REG_A);
    TCGv_i32 x      = load_byte_reg(RL78_BYTE_REG_X);
    TCGv_i32 result = tcg_temp_new_i32();

    tcg_gen_mul_i32(result, a, x);
    tcg_gen_andi_i32(result, result, 0xFFFF);

    store_word_reg(RL78_WORD_REG_AX, result);

    return true;
}

static bool trans_INC(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 src    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 result = tcg_temp_new_i32();

    tcg_gen_add_i32(result, src, tcg_constant_i32(1));

    rl78_gen_store_operand(ctx, insn->operand[0], result);

    tcg_gen_mov_i32(cpu_psw_ac, half_carry(src, tcg_constant_i32(1), result));
    tcg_gen_mov_i32(cpu_psw_z, zero_byte(result));

    return true;
}

static bool trans_DEC(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 src    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 result = tcg_temp_new_i32();

    tcg_gen_sub_i32(result, src, tcg_constant_i32(1));

    rl78_gen_store_operand(ctx, insn->operand[0], result);

    tcg_gen_mov_i32(cpu_psw_ac, half_borrow(src, tcg_constant_i32(1), result));
    tcg_gen_mov_i32(cpu_psw_z, zero_byte(result));

    return true;
}

static bool trans_INCW(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 src    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 result = tcg_temp_new_i32();

    tcg_gen_add_i32(result, src, tcg_constant_i32(1));
    rl78_gen_store_operand(ctx, insn->operand[0], result);

    return true;
}

static bool trans_DECW(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 src    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 result = tcg_temp_new_i32();

    tcg_gen_sub_i32(result, src, tcg_constant_i32(1));
    rl78_gen_store_operand(ctx, insn->operand[0], result);

    return true;
}

static bool shift_logical_right(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 src    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 shamt  = rl78_gen_load_operand(ctx, insn->operand[1]);
    TCGv_i32 result = tcg_temp_new_i32();

    tcg_gen_shli_i32(result, src, 1);
    tcg_gen_shr_i32(result, result, shamt);

    tcg_gen_andi_i32(cpu_psw_cy, result, 0x01);
    tcg_gen_shri_i32(result, result, 1);
    rl78_gen_store_operand(ctx, insn->operand[0], result);

    return true;
}

static bool shift_left(DisasContext *ctx, RL78Instruction *insn,
                       const uint opsize)
{
    TCGv_i32 src    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 shamt  = rl78_gen_load_operand(ctx, insn->operand[1]);
    TCGv_i32 result = tcg_temp_new_i32();

    tcg_gen_movi_i32(result, 0);
    tcg_gen_shl_i32(result, src, shamt);

    tcg_gen_shri_i32(cpu_psw_cy, result, opsize);
    tcg_gen_andi_i32(cpu_psw_cy, cpu_psw_cy, 0x01);

    rl78_gen_store_operand(ctx, insn->operand[0], result);

    return true;
}

static bool shift_arithmetic_right(DisasContext *ctx, RL78Instruction *insn,
                                   const uint opsize)
{
    TCGv_i32 src          = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 shamt        = rl78_gen_load_operand(ctx, insn->operand[1]);
    TCGv_i32 result       = tcg_temp_new_i32();
    const uint align_size = 32 - opsize;

    tcg_gen_movi_i32(result, 0);
    tcg_gen_shli_i32(result, src, align_size);
    tcg_gen_sar_i32(result, result, shamt);

    tcg_gen_mov_i32(cpu_psw_cy, result);
    tcg_gen_shri_i32(cpu_psw_cy, cpu_psw_cy, align_size - 1);
    tcg_gen_andi_i32(cpu_psw_cy, cpu_psw_cy, 0x01);

    tcg_gen_shri_i32(result, result, align_size);
    rl78_gen_store_operand(ctx, insn->operand[0], result);

    return true;
}

static bool trans_SHR(DisasContext *ctx, RL78Instruction *insn)
{
    return shift_logical_right(ctx, insn);
}

static bool trans_SHRW(DisasContext *ctx, RL78Instruction *insn)
{
    return shift_logical_right(ctx, insn);
}

static bool trans_SHL(DisasContext *ctx, RL78Instruction *insn)
{
    return shift_left(ctx, insn, 8);
}

static bool trans_SHLW(DisasContext *ctx, RL78Instruction *insn)
{
    return shift_left(ctx, insn, 16);
}

static bool trans_SAR(DisasContext *ctx, RL78Instruction *insn)
{
    return shift_arithmetic_right(ctx, insn, 8);
}

static bool trans_SARW(DisasContext *ctx, RL78Instruction *insn)
{
    return shift_arithmetic_right(ctx, insn, 16);
}

static bool trans_ROR(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 src    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 shamt  = rl78_gen_load_operand(ctx, insn->operand[1]);
    TCGv_i32 result = tcg_temp_new_i32();
    TCGv_i32 cutoff = tcg_temp_new_i32();

    tcg_gen_movi_i32(result, 0);
    tcg_gen_shli_i32(result, src, 24);
    tcg_gen_shr_i32(result, result, shamt);

    tcg_gen_andi_i32(cutoff, result, 0x00FF0000);
    tcg_gen_shli_i32(cutoff, cutoff, 8);
    tcg_gen_or_i32(result, result, cutoff);

    tcg_gen_shri_i32(result, result, 24);
    tcg_gen_shri_i32(cpu_psw_cy, result, 7);
    tcg_gen_andi_i32(cpu_psw_cy, cpu_psw_cy, 0x01);

    rl78_gen_store_operand(ctx, insn->operand[0], result);

    return true;
}

static bool trans_ROL(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 src    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 shamt  = rl78_gen_load_operand(ctx, insn->operand[1]);
    TCGv_i32 result = tcg_temp_new_i32();
    TCGv_i32 cutoff = tcg_temp_new_i32();

    tcg_gen_shl_i32(result, src, shamt);
    tcg_gen_shri_i32(cutoff, result, 8);
    tcg_gen_or_i32(result, result, cutoff);

    tcg_gen_andi_i32(cpu_psw_cy, result, 0x01);
    rl78_gen_store_operand(ctx, insn->operand[0], result);

    return true;
}

static bool trans_RORC(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 src    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 shamt  = rl78_gen_load_operand(ctx, insn->operand[1]);
    TCGv_i32 result = tcg_temp_new_i32();
    TCGv_i32 cutoff = tcg_temp_new_i32();

    tcg_gen_shli_i32(result, src, 1);
    tcg_gen_or_i32(result, result, cpu_psw_cy);
    tcg_gen_rotr_i32(result, result, shamt);

    tcg_gen_andi_i32(cpu_psw_cy, result, 0x01);
    tcg_gen_shri_i32(cutoff, result, 24);
    tcg_gen_shri_i32(result, result, 1);
    tcg_gen_or_i32(result, result, cutoff);

    rl78_gen_store_operand(ctx, insn->operand[0], result);

    return true;
}

static bool trans_ROLC(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 src    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 shamt  = rl78_gen_load_operand(ctx, insn->operand[1]);
    TCGv_i32 result = tcg_temp_new_i32();
    TCGv_i32 cutoff = tcg_temp_new_i32();

    tcg_gen_shli_i32(src, src, 24);
    tcg_gen_or_i32(src, src, cpu_psw_cy);

    tcg_gen_rotl_i32(result, src, shamt);

    tcg_gen_andi_i32(cpu_psw_cy, result, 0x01);

    tcg_gen_shri_i32(cutoff, result, 1);
    tcg_gen_andi_i32(cutoff, cutoff, 0xFF);

    tcg_gen_shri_i32(result, result, 24);
    tcg_gen_or_i32(result, result, cutoff);
    rl78_gen_store_operand(ctx, insn->operand[0], result);

    return true;
}

static bool trans_ROLWC(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 src    = rl78_gen_load_operand(ctx, insn->operand[0]);
    TCGv_i32 shamt  = rl78_gen_load_operand(ctx, insn->operand[1]);
    TCGv_i32 result = tcg_temp_new_i32();
    TCGv_i32 cutoff = tcg_temp_new_i32();

    tcg_gen_shli_i32(src, src, 16);
    tcg_gen_or_i32(src, src, cpu_psw_cy);

    tcg_gen_rotl_i32(result, src, shamt);

    tcg_gen_andi_i32(cpu_psw_cy, result, 0x01);

    tcg_gen_shri_i32(cutoff, result, 1);
    tcg_gen_andi_i32(cutoff, cutoff, 0xFFFF);

    tcg_gen_shri_i32(result, result, 16);
    tcg_gen_or_i32(result, result, cutoff);
    rl78_gen_store_operand(ctx, insn->operand[0], result);

    return true;
}

static bool trans_MOV1(DisasContext *ctx, RL78Instruction *insn)
{
    RL78BitData bitop = rl78_gen_load_bit(ctx, insn->operand[1].bit);
    rl78_gen_store_bit(ctx, insn->operand[0].bit, bitop);

    return true;
}

static bool trans_AND1(DisasContext *ctx, RL78Instruction *insn)
{
    RL78BitData dst = rl78_gen_load_bit(ctx, insn->operand[0].bit);
    RL78BitData src = rl78_gen_load_bit(ctx, insn->operand[1].bit);

    tcg_gen_and_i32(dst.bit, dst.bit, src.bit);
    rl78_gen_store_bit(ctx, insn->operand[0].bit, dst);

    return true;
}

static bool trans_OR1(DisasContext *ctx, RL78Instruction *insn)
{
    RL78BitData dst = rl78_gen_load_bit(ctx, insn->operand[0].bit);
    RL78BitData src = rl78_gen_load_bit(ctx, insn->operand[1].bit);

    tcg_gen_or_i32(dst.bit, dst.bit, src.bit);
    rl78_gen_store_bit(ctx, insn->operand[0].bit, dst);

    return true;
}

static bool trans_XOR1(DisasContext *ctx, RL78Instruction *insn)
{
    RL78BitData dst = rl78_gen_load_bit(ctx, insn->operand[0].bit);
    RL78BitData src = rl78_gen_load_bit(ctx, insn->operand[1].bit);

    tcg_gen_xor_i32(dst.bit, dst.bit, src.bit);
    rl78_gen_store_bit(ctx, insn->operand[0].bit, dst);

    return true;
}

static bool trans_SET1(DisasContext *ctx, RL78Instruction *insn)
{
    RL78BitData dst = rl78_gen_load_bit(ctx, insn->operand[0].bit);

    tcg_gen_movi_i32(dst.bit, 1);
    rl78_gen_store_bit(ctx, insn->operand[0].bit, dst);

    return true;
}

static bool trans_CLR1(DisasContext *ctx, RL78Instruction *insn)
{
    RL78BitData dst = rl78_gen_load_bit(ctx, insn->operand[0].bit);

    tcg_gen_movi_i32(dst.bit, 0);
    rl78_gen_store_bit(ctx, insn->operand[0].bit, dst);

    return true;
}

static bool trans_NOT1(DisasContext *ctx, RL78Instruction *insn)
{
    RL78BitData dst = rl78_gen_load_bit(ctx, insn->operand[0].bit);

    tcg_gen_xori_i32(dst.bit, dst.bit, 0x01);
    rl78_gen_store_bit(ctx, insn->operand[0].bit, dst);

    return true;
}

static void rl78_gen_prepare_call(DisasContext *ctx, const bool save_psw)
{
    const uint ret_pc  = ctx->base.pc_next;
    const uint ret_pcs = (ret_pc >> 16) & 0x0F;
    const uint ret_pch = (ret_pc >> 8) & 0xFF;
    const uint ret_pcl = (ret_pc >> 0) & 0xFF;

    tcg_gen_subi_i32(cpu_sp, cpu_sp, 1);
    if (save_psw) {
        TCGv_i32 psw = load_psw();
        rl78_gen_sb(ctx, rl78_gen_addr(ctx, cpu_sp), psw);
    }

    tcg_gen_subi_i32(cpu_sp, cpu_sp, 1);
    rl78_gen_sb(ctx, rl78_gen_addr(ctx, cpu_sp), tcg_constant_i32(ret_pcs));
    tcg_gen_subi_i32(cpu_sp, cpu_sp, 1);
    rl78_gen_sb(ctx, rl78_gen_addr(ctx, cpu_sp), tcg_constant_i32(ret_pch));
    tcg_gen_subi_i32(cpu_sp, cpu_sp, 1);
    rl78_gen_sb(ctx, rl78_gen_addr(ctx, cpu_sp), tcg_constant_i32(ret_pcl));
}

static bool call_ind_reg(DisasContext *ctx, RL78WordRegister reg)
{
    TCGv_i32 target = load_word_reg(reg);
    TCGv_i32 pc_s   = tcg_temp_new_i32();

    tcg_gen_mov_i32(pc_s, cpu_cs);
    tcg_gen_shli_i32(pc_s, pc_s, 16);
    tcg_gen_add_i32(target, target, pc_s);

    rl78_gen_prepare_call(ctx, false);
    tcg_gen_mov_i32(cpu_pc, target);

    tcg_gen_lookup_and_goto_ptr();
    ctx->base.is_jmp = DISAS_NORETURN;

    return true;
}

static void call_abs(DisasContext *ctx, vaddr target)
{
    rl78_gen_prepare_call(ctx, false);
    rl78_gen_goto_tb(ctx, TB_EXIT_JUMP, target);
}

static void call_rel(DisasContext *ctx, int32_t rel)
{
    const vaddr target = ctx->base.pc_next + rel;

    call_abs(ctx, target);
}

static bool trans_CALL(DisasContext *ctx, RL78Instruction *insn)
{
    RL78Operand target_op = insn->operand[0];

    switch (target_op.kind) {
    case RL78_OP_WORD_REG:
        call_ind_reg(ctx, target_op.word_reg);
        break;
    case RL78_OP_ABS16: {
        const uint32_t target = target_op.const_op & 0xFFFF;
        call_abs(ctx, target);
        break;
    }
    case RL78_OP_ABS20: {
        const uint32_t target = target_op.const_op & 0xFFFFF;
        call_abs(ctx, target);
        break;
    }
    case RL78_OP_REL16: {
        const int32_t rel = (int16_t)(target_op.const_op & 0xFFFF);
        call_rel(ctx, rel);
        break;
    }
    default:
        // TODO: raise implementation error assert
        break;
    }

    return true;
}

static bool trans_CALLT(DisasContext *ctx, RL78Instruction *insn)
{
    const vaddr addr = insn->operand[0].const_op;
    TCGv_i32 target  = rl78_gen_lw_static(ctx, addr);

    rl78_gen_prepare_call(ctx, target);
    tcg_gen_mov_i32(cpu_pc, target);
    tcg_gen_lookup_and_goto_ptr();
    ctx->base.is_jmp = DISAS_NORETURN;

    return true;
}

static bool trans_BRK(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 target = rl78_gen_lw_static(ctx, 0x0007E);

    rl78_gen_prepare_call(ctx, true);
    tcg_gen_movi_i32(cpu_psw_ie, 0);

    tcg_gen_mov_i32(cpu_pc, target);
    tcg_gen_lookup_and_goto_ptr();
    ctx->base.is_jmp = DISAS_NORETURN;

    return true;
}

static void rl78_gen_ret(DisasContext *ctx, bool restore_psw)
{
    TCGv_i32 pcl, pch, pcs;
    TCGv_i32 target = tcg_temp_new_i32();

    pcl = rl78_gen_lb(ctx, rl78_gen_addr(ctx, cpu_sp));
    tcg_gen_addi_i32(cpu_sp, cpu_sp, 1);
    pch = rl78_gen_lb(ctx, rl78_gen_addr(ctx, cpu_sp));
    tcg_gen_addi_i32(cpu_sp, cpu_sp, 1);
    pcs = rl78_gen_lb(ctx, rl78_gen_addr(ctx, cpu_sp));
    tcg_gen_addi_i32(cpu_sp, cpu_sp, 1);
    if (restore_psw) {
        TCGv_i32 psw = rl78_gen_lb(ctx, rl78_gen_addr(ctx, cpu_sp));
        store_psw(psw);
    }
    tcg_gen_addi_i32(cpu_sp, cpu_sp, 1);

    tcg_gen_shli_i32(pch, pch, 8);
    tcg_gen_shli_i32(pcs, pcs, 16);

    tcg_gen_mov_i32(target, pcl);
    tcg_gen_or_i32(target, target, pch);
    tcg_gen_or_i32(target, target, pcs);

    tcg_gen_mov_i32(cpu_pc, target);
    tcg_gen_lookup_and_goto_ptr();
    ctx->base.is_jmp = DISAS_NORETURN;
}

static bool trans_RET(DisasContext *ctx, RL78Instruction *insn)
{
    rl78_gen_ret(ctx, false);
    return true;
}

static bool trans_RETI(DisasContext *ctx, RL78Instruction *insn)
{
    rl78_gen_ret(ctx, true);
    return true;
}

static bool trans_RETB(DisasContext *ctx, RL78Instruction *insn)
{
    rl78_gen_ret(ctx, true);
    return true;
}

static bool trans_PUSH(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 data = rl78_gen_load_operand(ctx, insn->operand[0]);
    tcg_gen_subi_i32(cpu_sp, cpu_sp, 2);
    TCGv_i32 addr = rl78_gen_addr(ctx, cpu_sp);
    rl78_gen_sw(ctx, addr, data);

    return true;
}

static bool trans_POP(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 addr = rl78_gen_addr(ctx, cpu_sp);
    TCGv_i32 data = rl78_gen_lw(ctx, addr);
    tcg_gen_addi_i32(cpu_sp, cpu_sp, 2);
    rl78_gen_store_operand(ctx, insn->operand[0], data);

    return true;
}

static void rl78_gen_ind_jump(DisasContext *ctx, RL78WordRegister reg)
{
    TCGv_i32 target = load_word_reg(reg);
    TCGv_i32 pc_s   = tcg_temp_new_i32();

    tcg_gen_mov_i32(pc_s, cpu_cs);
    tcg_gen_shli_i32(pc_s, pc_s, 16);
    tcg_gen_add_i32(pc_s, pc_s, target);

    tcg_gen_mov_i32(cpu_pc, target);
    tcg_gen_lookup_and_goto_ptr();
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void rl78_gen_abs_jump(DisasContext *ctx, vaddr target)
{
    rl78_gen_goto_tb(ctx, TB_EXIT_JUMP, target);
}

static void rl78_gen_rel_jump(DisasContext *ctx, int32_t rel)
{
    const vaddr target = ctx->base.pc_next + rel;
    rl78_gen_goto_tb(ctx, TB_EXIT_JUMP, target);
}

static bool trans_BR(DisasContext *ctx, RL78Instruction *insn)
{
    RL78Operand op = insn->operand[0];

    switch (op.kind) {
    case RL78_OP_WORD_REG:
        rl78_gen_ind_jump(ctx, op.word_reg);
        break;
    case RL78_OP_ABS16:
        rl78_gen_abs_jump(ctx, op.const_op & 0xFFFF);
        break;
    case RL78_OP_ABS20:
        rl78_gen_abs_jump(ctx, op.const_op & 0xFFFFF);
        break;
    case RL78_OP_REL8:
        rl78_gen_rel_jump(ctx, (int8_t)(op.const_op & 0xFF));
        break;
    case RL78_OP_REL16:
        rl78_gen_rel_jump(ctx, (int16_t)(op.const_op & 0xFFFF));
    default:
        // TODO: raise implementation error assert
        break;
    }

    return true;
}

static void rl78_gen_branch(DisasContext *ctx, RL78Instruction *insn,
                            TCGCond cond, TCGv_i32 operand)
{
    const int32_t rel  = (int8_t)(insn->operand[0].const_op & 0xFF);
    const vaddr target = ctx->base.pc_next + rel;
    TCGLabel *nobranch = gen_new_label();

    tcg_gen_brcondi_i32(cond, operand, 0, nobranch);
    rl78_gen_goto_tb(ctx, TB_EXIT_BRANCH, target);

    gen_set_label(nobranch);
    rl78_gen_goto_tb(ctx, TB_EXIT_NOBRANCH, ctx->base.pc_next);
}

static bool trans_BC(DisasContext *ctx, RL78Instruction *insn)
{
    rl78_gen_branch(ctx, insn, TCG_COND_EQ, cpu_psw_cy);
    return true;
}

static bool trans_BNC(DisasContext *ctx, RL78Instruction *insn)
{
    rl78_gen_branch(ctx, insn, TCG_COND_NE, cpu_psw_cy);
    return true;
}

static bool trans_BZ(DisasContext *ctx, RL78Instruction *insn)
{
    rl78_gen_branch(ctx, insn, TCG_COND_EQ, cpu_psw_z);
    return true;
}

static bool trans_BNZ(DisasContext *ctx, RL78Instruction *insn)
{
    rl78_gen_branch(ctx, insn, TCG_COND_NE, cpu_psw_z);
    return true;
}

static bool trans_BH(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 operand = tcg_temp_new_i32();

    tcg_gen_mov_i32(operand, cpu_psw_z);
    tcg_gen_or_i32(operand, operand, cpu_psw_cy);

    rl78_gen_branch(ctx, insn, TCG_COND_EQ, operand);
    return true;
}

static bool trans_BNH(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 operand = tcg_temp_new_i32();

    tcg_gen_mov_i32(operand, cpu_psw_z);
    tcg_gen_or_i32(operand, operand, cpu_psw_cy);

    rl78_gen_branch(ctx, insn, TCG_COND_NE, operand);
    return true;
}

static bool trans_BT(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 operand = rl78_gen_load_operand(ctx, insn->operand[0]);

    rl78_gen_branch(ctx, insn, TCG_COND_EQ, operand);

    return true;
}

static bool trans_BF(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 operand = rl78_gen_load_operand(ctx, insn->operand[0]);
    rl78_gen_branch(ctx, insn, TCG_COND_NE, operand);
    return true;
}

static bool trans_BTCLR(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 operand   = rl78_gen_load_operand(ctx, insn->operand[0]);
    const int32_t rel  = (int8_t)(insn->operand[0].const_op & 0xFF);
    const vaddr target = ctx->base.pc_next + rel;
    TCGLabel *nobranch = gen_new_label();

    tcg_gen_brcondi_i32(TCG_COND_EQ, operand, 0, nobranch);
    rl78_gen_store_operand(ctx, insn->operand[0], tcg_constant_i32(0));
    rl78_gen_goto_tb(ctx, TB_EXIT_BRANCH, target);

    gen_set_label(nobranch);
    rl78_gen_goto_tb(ctx, TB_EXIT_NOBRANCH, ctx->base.pc_next);

    return true;
}

static void rl78_gen_skip(DisasContext *ctx, TCGCond cond, TCGv_i32 operand)
{
    ctx->skip_flag = true;
    tcg_gen_movi_i32(cpu_skip_en, 1);
    tcg_gen_movcond_i32(cond, cpu_skip_req, operand, tcg_constant_i32(1),
                        tcg_constant_i32(1), tcg_constant_i32(0));
}

static bool trans_SKC(DisasContext *ctx, RL78Instruction *insn)
{
    rl78_gen_skip(ctx, TCG_COND_EQ, cpu_psw_cy);
    return true;
}

static bool trans_SKNC(DisasContext *ctx, RL78Instruction *insn)
{
    rl78_gen_skip(ctx, TCG_COND_NE, cpu_psw_cy);
    return true;
}

static bool trans_SKZ(DisasContext *ctx, RL78Instruction *insn)
{
    rl78_gen_skip(ctx, TCG_COND_EQ, cpu_psw_z);
    return true;
}

static bool trans_SKNZ(DisasContext *ctx, RL78Instruction *insn)
{
    rl78_gen_skip(ctx, TCG_COND_NE, cpu_psw_z);
    return true;
}

static bool trans_SKH(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 operand = tcg_temp_new_i32();
    tcg_gen_mov_i32(operand, cpu_psw_z);
    tcg_gen_or_i32(operand, operand, cpu_psw_cy);

    rl78_gen_skip(ctx, TCG_COND_EQ, operand);

    return true;
}

static bool trans_SKNH(DisasContext *ctx, RL78Instruction *insn)
{
    TCGv_i32 operand = tcg_temp_new_i32();
    tcg_gen_mov_i32(operand, cpu_psw_z);
    tcg_gen_or_i32(operand, operand, cpu_psw_cy);

    rl78_gen_skip(ctx, TCG_COND_NE, operand);

    return true;
}

static bool trans_SEL(DisasContext *ctx, RL78Instruction *insn)
{
    const uint32_t sel = insn->operand[0].const_op;

    tcg_gen_movi_i32(cpu_psw_rbs, sel & 0x03);

    return true;
}

static bool trans_NOP(DisasContext *ctx, RL78Instruction *insn) { return true; }

static bool trans_HALT(DisasContext *ctx, RL78Instruction *insn)
{
    tcg_gen_movi_i32(cpu_pc, ctx->base.pc_next);
    gen_helper_halt(tcg_env);
    ctx->base.is_jmp = DISAS_EXIT;

    return true;
}

static bool trans_STOP(DisasContext *ctx, RL78Instruction *insn)
{
    tcg_gen_movi_i32(cpu_pc, ctx->base.pc_next);
    gen_helper_halt(tcg_env);
    ctx->base.is_jmp = DISAS_EXIT;

    return true;
}

static void rl78_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    ctx->env          = cpu_env(cs);
    ctx->tb_flags     = ctx->base.tb->flags;
}

static void rl78_tr_tb_start(DisasContextBase *dcbase, CPUState *cs) {}

static void rl78_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_next, 0, 0);
}

static uint32_t rl78_tr_get_pc(DisasContext *ctx) { return ctx->base.pc_next; }

static void rl78_tr_set_pc(DisasContext *ctx, uint32_t pc)
{
    ctx->base.pc_next = pc;
}

static void rl78_tr_set_es(DisasContext *ctx, bool es) { ctx->use_es = es; }

static uint8_t rl78_tr_load_byte(DisasContext *ctx, uint32_t pc)
{
    return translator_ldub(ctx->env, &ctx->base, pc);
}

static TranslateHandler translator_table[] = {
    [RL78_INSN_MOV] = trans_MOV,     [RL78_INSN_XCH] = trans_XCH,
    [RL78_INSN_ONEB] = trans_ONEB,   [RL78_INSN_CLRB] = trans_CLRB,

    [RL78_INSN_MOVW] = trans_MOVW,   [RL78_INSN_XCHW] = trans_XCHW,
    [RL78_INSN_ONEW] = trans_ONEW,   [RL78_INSN_CLRW] = trans_CLRW,

    [RL78_INSN_ADD] = trans_ADD,     [RL78_INSN_ADDC] = trans_ADDC,
    [RL78_INSN_SUB] = trans_SUB,     [RL78_INSN_SUBC] = trans_SUBC,
    [RL78_INSN_AND] = trans_AND,     [RL78_INSN_OR] = trans_OR,
    [RL78_INSN_XOR] = trans_XOR,     [RL78_INSN_CMP] = trans_CMP,
    [RL78_INSN_CMP0] = trans_CMP0,   [RL78_INSN_CMPS] = trans_CMPS,
    [RL78_INSN_MOVS] = trans_MOVS,

    [RL78_INSN_ADDW] = trans_ADDW,   [RL78_INSN_SUBW] = trans_SUBW,
    [RL78_INSN_CMPW] = trans_CMPW,

    [RL78_INSN_MULU] = trans_MULU,

    [RL78_INSN_INC] = trans_INC,     [RL78_INSN_DEC] = trans_DEC,
    [RL78_INSN_INCW] = trans_INCW,   [RL78_INSN_DECW] = trans_DECW,

    [RL78_INSN_SHR] = trans_SHR,     [RL78_INSN_SHRW] = trans_SHRW,
    [RL78_INSN_SHL] = trans_SHL,     [RL78_INSN_SHLW] = trans_SHLW,
    [RL78_INSN_SAR] = trans_SAR,     [RL78_INSN_SARW] = trans_SARW,

    [RL78_INSN_ROR] = trans_ROR,     [RL78_INSN_ROL] = trans_ROL,
    [RL78_INSN_RORC] = trans_RORC,   [RL78_INSN_ROLC] = trans_ROLC,
    [RL78_INSN_ROLWC] = trans_ROLWC,

    [RL78_INSN_MOV1] = trans_MOV1,   [RL78_INSN_AND1] = trans_AND1,
    [RL78_INSN_OR1] = trans_OR1,     [RL78_INSN_XOR1] = trans_XOR1,
    [RL78_INSN_SET1] = trans_SET1,   [RL78_INSN_CLR1] = trans_CLR1,
    [RL78_INSN_NOT1] = trans_NOT1,

    [RL78_INSN_CALL] = trans_CALL,   [RL78_INSN_CALLT] = trans_CALLT,
    [RL78_INSN_BRK] = trans_BRK,     [RL78_INSN_RET] = trans_RET,
    [RL78_INSN_RETI] = trans_RETI,   [RL78_INSN_RETB] = trans_RETB,

    [RL78_INSN_PUSH] = trans_PUSH,   [RL78_INSN_POP] = trans_POP,

    [RL78_INSN_BR] = trans_BR,       [RL78_INSN_BC] = trans_BC,
    [RL78_INSN_BNC] = trans_BNC,     [RL78_INSN_BZ] = trans_BZ,
    [RL78_INSN_BNZ] = trans_BNZ,     [RL78_INSN_BH] = trans_BH,
    [RL78_INSN_BNH] = trans_BNH,     [RL78_INSN_BT] = trans_BT,
    [RL78_INSN_BF] = trans_BF,       [RL78_INSN_BTCLR] = trans_BTCLR,

    [RL78_INSN_SKC] = trans_SKC,     [RL78_INSN_SKNC] = trans_SKNC,
    [RL78_INSN_SKZ] = trans_SKZ,     [RL78_INSN_SKNZ] = trans_SKNZ,
    [RL78_INSN_SKH] = trans_SKH,     [RL78_INSN_SKNH] = trans_SKNH,

    [RL78_INSN_SEL] = trans_SEL,     [RL78_INSN_NOP] = trans_NOP,
    [RL78_INSN_HALT] = trans_HALT,   [RL78_INSN_STOP] = trans_STOP,
};

static void rl78_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx           = container_of(dcbase, DisasContext, base);
    RL78CPU *cpu                = RL78_CPU(cs);
    TCGLabel *skip_label        = NULL;
    const DecodeHandler handler = {
        .get_pc           = rl78_tr_get_pc,
        .set_pc           = rl78_tr_set_pc,
        .set_es           = rl78_tr_set_es,
        .load_byte        = rl78_tr_load_byte,
        .translator_table = translator_table,
    };

    ctx->standard_sfr = cpu->standard_sfr;
    ctx->extended_sfr = cpu->extended_sfr;
    ctx->mirror       = cpu->mirror;

    const bool use_skip = ctx->skip_flag;
    ctx->skip_flag      = false;
    if (use_skip) {
        TCGv_i32 required_tmp = tcg_temp_new_i32();

        tcg_gen_movi_i32(cpu_skip_en, 0);
        tcg_gen_mov_i32(required_tmp, cpu_skip_req);
        tcg_gen_movi_i32(cpu_skip_req, 0);

        skip_label = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_NE, required_tmp, 0, skip_label);
    }

    ctx->pc = ctx->base.pc_next;

    vaddr head_pc = ctx->pc;
    if (!decode(ctx, &handler)) {
        error_report("Failed to decode instruction at PC: %08X[byte: %d]",
                     (uint32_t)head_pc,
                     (uint32_t)(ctx->base.pc_next - head_pc));
        exit(1);
        // TODO: gen_helper_raise_illegal_instruction(tcg_env);
    }

    if (use_skip) {
        gen_set_label(skip_label);
    }
}

static void rl78_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_NEXT:
    case DISAS_EXIT:
    case DISAS_NORETURN:
        break;
    case DISAS_TOO_MANY:
        rl78_gen_goto_tb(ctx, TB_EXIT_IDX0, ctx->base.pc_next);
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

void rl78_translate_code(CPUState *cs, TranslationBlock *tb, int *max_insns,
                         vaddr pc, void *host_pc)
{
    DisasContext dc;
    CPUArchState *env = cpu_env(cs);

    dc.env       = env;
    dc.pc        = pc;
    dc.tb_flags  = tb->flags;
    dc.skip_flag = env->skip_en;

    translator_loop(cs, tb, max_insns, pc, host_pc, &rl78_tr_ops, &dc.base);
}

#define ALLOC_REGISTER(sym, member, name)                                      \
    cpu_##sym =                                                                \
        tcg_global_mem_new_i32(tcg_env, offsetof(CPURL78State, member), name)

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
