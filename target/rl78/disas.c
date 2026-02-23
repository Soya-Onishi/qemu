#include "qemu/osdep.h"
#include "disas/dis-asm.h"
#include "qemu/bitops.h"
#include "cpu.h"
#include "qemu/win_dump_defs.h"

typedef struct DisasContext {
    disassemble_info *dis;

    uint32_t addr;
    uint32_t pc;

    uint8_t len;
    uint8_t bytes[5];
} DisasContext;

static uint64_t decode_load_bytes(DisasContext *ctx, uint64_t insn, int i,
                                  int n)
{
    const int cnt = n - i;

    for (int offset = 0; offset < cnt; offset++) {
        const uint32_t addr = ctx->addr + offset;
        const uint shamt    = 64 - (i + offset + 1) * 8;
        uint8_t *b          = &ctx->bytes[i + offset];

        ctx->dis->read_memory_func(addr, b, 1, ctx->dis);
        insn |= (uint64_t)*b << shamt;
    }
    ctx->addr += cnt;
    ctx->len   = n;

    return insn;
}

static void dump_bytes(DisasContext *ctx)
{
    const uint len = ctx->len;
    for (int i = 0; i < len; i++) {
        ctx->dis->fprintf_func(ctx->dis->stream, "%02x ", ctx->bytes[i]);
    }

    ctx->dis->fprintf_func(ctx->dis->stream, "%*c", (5 - len) * 3, '\t');
}
#define print_0(mnemonic)                                                      \
    do {                                                                       \
        dump_bytes(ctx);                                                       \
        ctx->dis->fprintf_func(ctx->dis->stream, "%-6s", mnemonic);            \
    } while (0)

#define print_1(mnemonic, op0)                                                 \
    do {                                                                       \
        dump_bytes(ctx);                                                       \
        ctx->dis->fprintf_func(ctx->dis->stream, "%-6s\t%s", mnemonic, op0);   \
    } while (0)

#define print_2(mnemonic, op0, op1)                                            \
    do {                                                                       \
        dump_bytes(ctx);                                                       \
        ctx->dis->fprintf_func(ctx->dis->stream, "%-6s\t%s, %s", mnemonic,     \
                               op0, op1);                                      \
    } while (0)

#define print_N(_mnemonic, _1, _2, N) print_##N

#define print(...) print_N(__VA_ARGS__, 2, 1, 0)(__VA_ARGS__)

static uint32_t rl78_word(uint32_t raw)
{
    const uint32_t lower = (raw & 0x0000FF00) >> 8;
    const uint32_t upper = (raw & 0x000000FF) << 8;

    return lower | upper;
}

static const char *es(void) { return "ES"; }
static const char *cs(void) { return "CS"; }
static const char *sp(void) { return "SP"; }
static const char *psw(void) { return "PSW"; }

static const char *byte_reg(RL78ByteRegister reg)
{
    static const char *regs[] = {
        "X", "A", "C", "B", "E", "D", "L", "H",
    };

    return regs[reg];
}

static const char *word_reg(RL78WordRegister reg)
{
    static const char *regs[] = {
        "AX",
        "BC",
        "DE",
        "HL",
    };

    return regs[reg];
}

static char *imm8(uint32_t imm) { return g_strdup_printf("#%d", imm); }

static char *imm16(uint32_t imm)
{
    const uint32_t word = rl78_word(imm);
    return g_strdup_printf("#%d", word);
}

static char *abs16(uint32_t addr, bool use_es)
{
    const char *prefix  = use_es ? "ES:" : "";
    const uint32_t word = rl78_word(addr);

    return g_strdup_printf("%s!0x%04x", prefix, word);
}

static char *saddr(uint32_t saddr)
{
    const uint32_t base = saddr < 0x20 ? 0xFFF00 : 0xFFE00;
    const uint32_t abs  = base + saddr;

    return g_strdup_printf("!0x%04x", abs);
}

static char *sfr(uint32_t sfr)
{
    const uint32_t abs = 0xFFF00 + sfr;
    return g_strdup_printf("!0x%04x", abs);
}

static char *abs20(const uint32_t addr)
{
    const uint32_t adrl = ((addr & 0x00FF0000) >> 16) << 0;
    const uint32_t adrh = ((addr & 0x0000FF00) >> 8) << 8;
    const uint32_t adrs = ((addr & 0x000000FF) >> 0) << 16;
    const uint32_t dest = adrl | adrh | adrs;

    return g_strdup_printf("!!0x%05x", dest);
}

static char *ind_reg_idx(const char *reg, const char *offset, const bool use_es)
{
    const char *prefix = use_es ? "ES:" : "";
    const char *offset_str =
        offset == NULL ? "" : g_strdup_printf(" + %s", offset);

    return g_strdup_printf("%s[%s%s]", prefix, reg, offset_str);
}

static char *ind_base_idx(const uint32_t base, const char *reg, bool use_es)
{
    const char *prefix     = use_es ? "ES:" : "";
    const uint32_t base_le = rl78_word(base);

    return g_strdup_printf("%s0x%04x[%s]", prefix, base_le, reg);
}

static char *rel8(const uint32_t pc, const int32_t rel)
{
    const uint32_t dst = pc + rel;
    return g_strdup_printf("$%d", dst);
}

static char *rel16(const uint32_t pc, const int32_t rel)
{
    const uint32_t dst = pc + rel;
    return g_strdup_printf("$!%d", dst);
}

static char *bit(const char *base, const uint bit)
{
    return g_strdup_printf("%s.%d", base, bit);
}

#include "decode-insn.c.inc"

static bool trans_NOP(DisasContext *ctx, arg_NOP *a)
{
    print("NOP");
    return true;
}

int print_insn_rl78(bfd_vma addr, disassemble_info *dis)
{
    DisasContext ctx;
    uint64_t insn;
    int i;

    ctx.dis = dis;
    ctx.pc = ctx.addr = addr;
    ctx.len           = 0;

    insn = decode_load(&ctx);
    if (!decode(&ctx, insn)) {
        ctx.dis->fprintf_func(ctx.dis->stream, ".byte\t");
        for (i = 0; i < ctx.addr - addr; i++) {
            if (i > 0) {
                ctx.dis->fprintf_func(ctx.dis->stream, ",");
            }
            ctx.dis->fprintf_func(ctx.dis->stream, "0x%02x",
                                  (uint8_t)(insn >> 56));
            insn <<= 8;
        }
    }

    return ctx.addr - addr;
}
