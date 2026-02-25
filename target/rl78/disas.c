#include "qemu/osdep.h"
#include "disas/dis-asm.h"
#include "qemu/bitops.h"
#include "cpu.h"
#include "qemu/win_dump_defs.h"
#include "decode.h"

typedef struct DisasContext {
    disassemble_info *dis;

    uint32_t addr;
    uint32_t pc;

    bool use_es;
} DisasContext;

static void dump_bytes(DisasContext *ctx)
{
    const uint len = ctx->pc - ctx->addr;
    for (int i = 0; i < len; i++) {
        uint8_t byte;
        ctx->dis->read_memory_func(ctx->addr + i, &byte, 1, ctx->dis);
        ctx->dis->fprintf_func(ctx->dis->stream, "%02x ", byte);
    }

    ctx->dis->fprintf_func(ctx->dis->stream, "%*c", (5 - len) * 3, '\t');
}
#define print_0(mnemonic)                                           \
    do {                                                            \
        dump_bytes(ctx);                                            \
        ctx->dis->fprintf_func(ctx->dis->stream, "%-6s", mnemonic); \
    } while (0)

#define print_1(mnemonic, op0)                                               \
    do {                                                                     \
        dump_bytes(ctx);                                                     \
        ctx->dis->fprintf_func(ctx->dis->stream, "%-6s\t%s", mnemonic, op0); \
    } while (0)

#define print_2(mnemonic, op0, op1)                                        \
    do {                                                                   \
        dump_bytes(ctx);                                                   \
        ctx->dis->fprintf_func(ctx->dis->stream, "%-6s\t%s, %s", mnemonic, \
                               op0, op1);                                  \
    } while (0)

static uint32_t rl78_word(uint32_t raw)
{
    const uint32_t lower = (raw & 0x0000FF00) >> 8;
    const uint32_t upper = (raw & 0x000000FF) << 8;

    return lower | upper;
}

static char *es(DisasContext *ctx, const RL78Operand op)
{
    return g_strdup_printf("ES");
}
static char *cy(DisasContext *ctx, const RL78Operand op)
{
    return g_strdup_printf("CY");
}
static char *sp(DisasContext *ctx, const RL78Operand op)
{
    return g_strdup_printf("SP");
}
static char *psw(DisasContext *ctx, const RL78Operand op)
{
    return g_strdup_printf("PSW");
}

static char *byte_reg(DisasContext *ctx, const RL78Operand op)
{
    static const char *regs[] = {
        "X", "A", "C", "B", "E", "D", "L", "H",
    };

    return g_strdup_printf("%s", regs[op.byte_reg]);
}

static char *word_reg(DisasContext *ctx, const RL78Operand op)
{
    static const char *regs[] = {
        "AX",
        "BC",
        "DE",
        "HL",
    };

    return g_strdup_printf("%s", regs[op.word_reg]);
}

static char *imm8(DisasContext *ctx, const RL78Operand op)
{
    return g_strdup_printf("#%d", op.const_op);
}

static char *imm16(DisasContext *ctx, const RL78Operand op)
{
    return g_strdup_printf("#%d", op.const_op);
}

static char *abs16(DisasContext *ctx, const RL78Operand op)
{
    const char *prefix = ctx->use_es ? "ES:" : "";

    return g_strdup_printf("%s!0x%04x", prefix, op.const_op);
}

static char *saddr(DisasContext *ctx, const RL78Operand op)
{
    const uint32_t base = op.const_op < 0x20 ? 0xFFF00 : 0xFFE00;
    const uint32_t abs  = base + op.const_op;

    return g_strdup_printf("!0x%05x", abs);
}

static char *sfr(DisasContext *ctx, const RL78Operand op)
{
    const uint32_t abs = 0xFFF00 + op.const_op;
    return g_strdup_printf("!0x%05x", abs);
}

static char *abs20(DisasContext *ctx, const RL78Operand op)
{
    return g_strdup_printf("!!0x%05x", op.const_op);
}

static char *ind_reg_reg(DisasContext *ctx, const RL78Operand op)
{
    const char *prefix         = ctx->use_es ? "ES:" : "";
    const RL78Operand base_reg = {
        .kind     = RL78_OP_WORD_REG,
        .word_reg = op.ind_reg_reg.base,
    };
    const RL78Operand idx_reg = {
        .kind     = RL78_OP_BYTE_REG,
        .byte_reg = op.ind_reg_reg.idx,
    };

    char *base   = word_reg(ctx, base_reg);
    char *idx    = byte_reg(ctx, idx_reg);
    char *op_str = g_strdup_printf("%s[%s+%s]", prefix, base, idx);

    g_free(base);
    g_free(idx);

    return op_str;
}

static char *ind_reg_imm(DisasContext *ctx, const RL78Operand op)
{
    const char *prefix      = ctx->use_es ? "ES:" : "";
    const RL78Operand regop = {
        .kind     = RL78_OP_WORD_REG,
        .word_reg = op.ind_reg_reg.base,
    };
    char *base         = word_reg(ctx, regop);
    const uint32_t imm = op.ind_reg_imm.imm;
    char *op_str       = g_strdup_printf("%s[%s+%d]", prefix, base, imm);

    g_free(base);

    return op_str;
}

static char *ind_sp_imm(DisasContext *ctx, const RL78Operand op)
{
    return g_strdup_printf("[SP+%d]", op.const_op);
}

static char *ind_base_byte(DisasContext *ctx, const RL78Operand op)
{
    const char *prefix      = ctx->use_es ? "ES:" : "";
    const RL78Operand regop = {
        .kind     = RL78_OP_BYTE_REG,
        .byte_reg = op.ind_base_byte.idx,
    };
    char *idx           = byte_reg(ctx, regop);
    const uint16_t base = op.ind_base_byte.base;
    char *op_str        = g_strdup_printf("%s0x%04x[%s]", prefix, base, idx);

    g_free(idx);

    return op_str;
}

static char *ind_base_word(DisasContext *ctx, const RL78Operand op)
{
    const char *prefix      = ctx->use_es ? "ES:" : "";
    const RL78Operand regop = {
        .kind     = RL78_OP_WORD_REG,
        .word_reg = op.ind_base_word.idx,
    };

    char *idx           = word_reg(ctx, regop);
    const uint16_t base = op.ind_base_word.base;
    char *op_str        = g_strdup_printf("%s0x%04x[%s]", prefix, base, idx);

    g_free(idx);

    return op_str;
}

static char *rel8(DisasContext *ctx, const RL78Operand op)
{
    const uint32_t dst = ctx->pc + (int8_t)(op.const_op);
    return g_strdup_printf("$0x%05x", dst);
}

static char *rel16(DisasContext *ctx, const RL78Operand op)
{
    const uint32_t dst = ctx->pc + (int16_t)(op.const_op);
    return g_strdup_printf("$!0x%05x", dst);
}

static char *shamt(DisasContext *ctx, const RL78Operand op)
{
    return g_strdup_printf("#%d", op.const_op);
}

static char *callt(DisasContext *ctx, const RL78Operand op)
{
    return g_strdup_printf("[0x%02x]", op.const_op);
}

static char *sel_rb(DisasContext *ctx, const RL78Operand op)
{
    return g_strdup_printf("RB%d", op.const_op);
}

static char *bit(DisasContext *ctx, const RL78Operand op)
{
    char *base;

    switch (op.bit.kind) {
    case RL78_BITOP_SADDR: {
        RL78Operand bitop = {
            .kind     = RL78_OP_SADDR,
            .const_op = op.bit.addr,
        };

        base = saddr(ctx, bitop);
        break;
    }
    case RL78_BITOP_SFR: {
        RL78Operand bitop = {
            .kind     = RL78_OP_SFR,
            .const_op = op.bit.addr,
        };
        base = sfr(ctx, bitop);
        break;
    }
    case RL78_BITOP_REG_A: {
        RL78Operand bitop = {
            .kind     = RL78_OP_BYTE_REG,
            .byte_reg = RL78_BYTE_REG_A,
        };
        base = byte_reg(ctx, bitop);
        break;
    }
    case RL78_BITOP_ABS16: {
        RL78Operand bitop = {
            .kind     = RL78_OP_ABS16,
            .const_op = op.bit.addr,
        };
        base = abs16(ctx, bitop);
        break;
    }
    case RL78_BITOP_IND_HL: {
        RL78Operand bitop = {
            .kind     = RL78_OP_IND_BASE_BYTE,
            .const_op = op.bit.addr,
        };
        base = ind_base_byte(ctx, bitop);
        break;
    }
    }

    return g_strdup_printf("%s.%d", base, op.bit.bit);
}

static char *unknown(DisasContext *ctx, const RL78Operand op)
{
    return g_strdup_printf("");
}

static char *(*print_operand_table[])(DisasContext *ctx,
                                      const RL78Operand op) = {
    [RL78_OP_BYTE_REG]      = byte_reg,
    [RL78_OP_WORD_REG]      = word_reg,
    [RL78_OP_PSW]           = psw,
    [RL78_OP_SP]            = sp,
    [RL78_OP_ES]            = es,
    [RL78_OP_CY]            = cy,
    [RL78_OP_BIT]           = bit,
    [RL78_OP_IMM8]          = imm8,
    [RL78_OP_IMM16]         = imm16,
    [RL78_OP_ABS16]         = abs16,
    [RL78_OP_ABS20]         = abs20,
    [RL78_OP_SADDR]         = saddr,
    [RL78_OP_SFR]           = sfr,
    [RL78_OP_IND_REG_REG]   = ind_reg_reg,
    [RL78_OP_IND_REG_IMM]   = ind_reg_imm,
    [RL78_OP_IND_SP_IMM]    = ind_sp_imm,
    [RL78_OP_IND_BASE_BYTE] = ind_base_byte,
    [RL78_OP_IND_BASE_WORD] = ind_base_word,
    [RL78_OP_SHAMT]         = shamt,
    [RL78_OP_REL8]          = rel8,
    [RL78_OP_REL16]         = rel16,
    [RL78_OP_CALLT]         = callt,
    [RL78_OP_SEL_RB]        = sel_rb,
    [RL78_OP_NONE]          = unknown,
};

static bool print_impl(DisasContext *ctx, const char *mnemonic,
                       const RL78Instruction *insn)
{
    RL78OperandKind kind0 = insn->operand[0].kind;
    RL78OperandKind kind1 = insn->operand[1].kind;
    char *op0             = print_operand_table[kind0](ctx, insn->operand[0]);
    char *op1             = print_operand_table[kind1](ctx, insn->operand[1]);

    const uint op_num = (kind0 != RL78_OP_NONE) + (kind1 != RL78_OP_NONE);
    switch (op_num) {
    case 0:
        print_0(mnemonic);
        break;
    case 1:
        print_1(mnemonic, op0);
        break;
    case 2:
        print_2(mnemonic, op0, op1);
        break;
    }

    g_free(op0);
    g_free(op1);

    return true;
}

#define trans(mnemonic)                                                    \
    static bool trans_##mnemonic(DisasContext *ctx, RL78Instruction *insn) \
    {                                                                      \
        return print_impl(ctx, #mnemonic, insn);                           \
    }

trans(MOV);
trans(XCH);
trans(ONEB);
trans(CLRB);

trans(MOVW);
trans(XCHW);
trans(ONEW);
trans(CLRW);

trans(ADD);
trans(ADDC);
trans(SUB);
trans(SUBC);
trans(AND);
trans(OR);
trans(XOR);
trans(CMP);
trans(CMP0);
trans(CMPS);
trans(MOVS);

trans(ADDW);
trans(SUBW);
trans(CMPW);

trans(MULU);

trans(INC);
trans(DEC);
trans(INCW);
trans(DECW);

trans(SHR);
trans(SHRW);
trans(SHL);
trans(SHLW);
trans(SAR);
trans(SARW);

trans(ROR);
trans(ROL);
trans(RORC);
trans(ROLC);
trans(ROLWC);

trans(MOV1);
trans(AND1);
trans(OR1);
trans(XOR1);
trans(SET1);
trans(CLR1);
trans(NOT1);

trans(CALL);
trans(CALLT);
trans(BRK);
trans(HALT);
trans(STOP);
trans(RET);
trans(RETI);
trans(RETB);

trans(PUSH);
trans(POP);

trans(BR);
trans(BC);
trans(BNC);
trans(BZ);
trans(BNZ);
trans(BH);
trans(BNH);
trans(BT);
trans(BF);
trans(BTCLR);

trans(SKC);
trans(SKNC);
trans(SKZ);
trans(SKNZ);
trans(SKH);
trans(SKNH);

trans(SEL);
trans(NOP);

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
    [RL78_INSN_BRK] = trans_BRK,     [RL78_INSN_HALT] = trans_HALT,
    [RL78_INSN_STOP] = trans_STOP,   [RL78_INSN_RET] = trans_RET,
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
};

static uint8_t load_byte(DisasContext *ctx, uint32_t pc)
{
    uint8_t byte;
    ctx->dis->read_memory_func(pc, &byte, 1, ctx->dis);
    return byte;
}

static void set_pc(DisasContext *ctx, uint32_t pc) { ctx->pc = pc; }

static uint32_t get_pc(DisasContext *ctx) { return ctx->pc; }

static void set_es(DisasContext *ctx, bool es) { ctx->use_es = es; }

int print_insn_rl78(bfd_vma addr, disassemble_info *dis)
{
    DisasContext ctx;
    DecodeHandler handler = {
        .load_byte        = load_byte,
        .set_pc           = set_pc,
        .get_pc           = get_pc,
        .set_es           = set_es,
        .translator_table = translator_table,
    };

    ctx.dis = dis;
    ctx.pc =  ctx.addr = addr;

    if (!decode(&ctx, &handler)) {
        ctx.dis->fprintf_func(ctx.dis->stream, ".byte\t");
        for (int i = 0; i < ctx.pc - addr; i++) {
            uint8_t byte;
            ctx.dis->read_memory_func(addr + i, &byte, 1, ctx.dis);

            if (i > 0) {
                ctx.dis->fprintf_func(ctx.dis->stream, ",");
            }
            ctx.dis->fprintf_func(ctx.dis->stream, "0x%02x", byte);
        }
    }

    return ctx.pc - ctx.addr;
}
