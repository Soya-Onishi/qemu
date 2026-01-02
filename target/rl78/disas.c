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

static const char* rl78_cpu_gp_regnames[GPREG_NUM] = {
    "X", "A", "C", "B", 
    "E", "D", "L", "H",
};

static uint64_t decode_load_bytes(DisasContext *ctx, uint64_t insn, 
                                  int i, int n) 
{
    const int cnt = n - i;

    for(int offset = 0; offset < cnt; offset++) {
        const uint32_t addr = ctx->addr + offset;
        const uint shamt = 64 - (i + offset + 1) * 8;
        uint8_t *b = &ctx->bytes[i + offset];

        ctx->dis->read_memory_func(addr, b, 1, ctx->dis);
        insn |= (uint64_t)*b << shamt;
    }
    ctx->addr += cnt;
    ctx->len = n;

    return insn;
}

static void dump_bytes(DisasContext *ctx)
{
    const uint len = ctx->len;
    for(int i = 0; i < len; i++)  {
        ctx->dis->fprintf_func(ctx->dis->stream, "%02x ", ctx->bytes[i]);
    }

    ctx->dis->fprintf_func(ctx->dis->stream, "%*c", (5 - len) * 3, '\t');
}

static inline uint32_t rl78_word(uint32_t v)
{
    return ((v & 0x0000FF) << 8) | ((v & 0x00FF00) >> 8);
}

static inline uint32_t rl78_gen_saddr(int saddr)
{
    const uint32_t base = saddr < 0x20 ? 0xFFF00 : 0xFFE00;
    return base + saddr;
}

#define print(...)                                             \
    do {                                                            \
        dump_bytes(ctx);                                            \
        ctx->dis->fprintf_func(ctx->dis->stream, __VA_ARGS__); \
    } while(0)

#include "decode-insn.c.inc"

static bool print_MOV_A_rs(DisasContext *ctx, RL78GPRegister rs)
{
    print("MOV\t%s, %s", rl78_cpu_gp_regnames[RL78_GPREG_A], rl78_cpu_gp_regnames[rs]);
    return true;
}

static bool print_MOV_rd_A(DisasContext *ctx, RL78GPRegister rd)
{
    print("MOV\t%s, %s", rl78_cpu_gp_regnames[rd], rl78_cpu_gp_regnames[RL78_GPREG_A]);
    return true;
}

static bool trans_MOV_ri(DisasContext *ctx, arg_MOV_ri *a)
{
    print("MOV\tR%d, #%d", a->rd, a->imm);
    return true;
}

static bool trans_MOV_PSW_i(DisasContext *ctx, arg_MOV_PSW_i *a)
{
    print("MOV\tPSW, #%d", a->imm);
    return true;
}

static bool trans_MOV_A_X(DisasContext *ctx, arg_MOV_A_X *a)
{
    return print_MOV_A_rs(ctx, RL78_GPREG_X);
}


static bool trans_MOV_A_C(DisasContext *ctx, arg_MOV_A_C *a)
{
    return print_MOV_A_rs(ctx, RL78_GPREG_C);
}

static bool trans_MOV_A_B(DisasContext *ctx, arg_MOV_A_B *a)
{
    return print_MOV_A_rs(ctx, RL78_GPREG_B);
}

static bool trans_MOV_A_E(DisasContext *ctx, arg_MOV_A_E *a)
{
    return print_MOV_A_rs(ctx, RL78_GPREG_E);
}

static bool trans_MOV_A_D(DisasContext *ctx, arg_MOV_A_D *a)
{
    return print_MOV_A_rs(ctx, RL78_GPREG_D);
}

static bool trans_MOV_A_L(DisasContext *ctx, arg_MOV_A_L *a)
{
    return print_MOV_A_rs(ctx, RL78_GPREG_L);
}

static bool trans_MOV_A_H(DisasContext *ctx, arg_MOV_A_H *a)
{
    return print_MOV_A_rs(ctx, RL78_GPREG_H);
}

static bool trans_MOV_X_A(DisasContext *ctx, arg_MOV_X_A *a)
{
    return print_MOV_rd_A(ctx, RL78_GPREG_X);
}

static bool trans_MOV_C_A(DisasContext *ctx, arg_MOV_C_A *a)
{
    return print_MOV_rd_A(ctx, RL78_GPREG_C);
}

static bool trans_MOV_B_A(DisasContext *ctx, arg_MOV_B_A *a)
{
    return print_MOV_rd_A(ctx, RL78_GPREG_B);
}

static bool trans_MOV_E_A(DisasContext *ctx, arg_MOV_E_A *a)
{
    return print_MOV_rd_A(ctx, RL78_GPREG_E);
}

static bool trans_MOV_D_A(DisasContext *ctx, arg_MOV_D_A *a)
{
    return print_MOV_rd_A(ctx, RL78_GPREG_D);
}

static bool trans_MOV_L_A(DisasContext *ctx, arg_MOV_L_A *a)
{
    return print_MOV_rd_A(ctx, RL78_GPREG_L);
}

static bool trans_MOV_H_A(DisasContext *ctx, arg_MOV_H_A *a)
{
    return print_MOV_rd_A(ctx, RL78_GPREG_H);
}

static bool trans_MOV_saddr_i(DisasContext *ctx, arg_MOV_saddr_i *a)
{
    print("MOV\t!0x%05x, #%d", a->saddr + 0xFFE20, a->imm);
    return true;
}

static bool trans_MOV_sfr_i(DisasContext *ctx, arg_MOV_sfr_i *a)
{
    print("MOV\t!0x%05x, #%d", a->sfr + 0xFFF00, a->imm);
    return true;
}

static bool trans_MOV_addr_i(DisasContext *ctx, arg_MOV_addr_i *a)
{
    print("MOV\t!0x%05x, #%d", rl78_word(a->addr) + 0xF0000, a->imm);
    return true;
}

static bool trans_MOV_addr_r(DisasContext *ctx, arg_MOV_addr_r *a)
{
    print("MOV\t!0x%05x, A", rl78_word(a->addr) + 0xF0000);
    return true;
}

static bool trans_MOV_A_indDE(DisasContext *ctx, arg_MOV_A_indDE *a)
{
    print("MOV\tA, [DE]");
    return true;
}

static bool trans_MOV_indDE_A(DisasContext *ctx, arg_MOV_indDE_A *a)
{
    print("MOV\t[DE], A");
    return true;
}

static bool trans_MOV_indDEoffset_i(DisasContext *ctx, arg_MOV_indDEoffset_i *a)
{
    print("MOV\t[DE+%d], #%d", a->offset, a->imm);
    return true;
}

static bool trans_MOV_A_indDEoffset(DisasContext *ctx, arg_MOV_A_indDEoffset *a)
{
    print("MOV\tA, [DE+%d]", a->offset);
    return true;
}

static bool trans_MOV_indDEoffset_A(DisasContext *ctx, arg_MOV_indDEoffset_A *a)
{
    print("MOV\t[DE+%d], A", a->offset);
    return true;
}

static bool trans_MOV_PSW_A(DisasContext *ctx, arg_MOV_PSW_A *a)
{
    print("MOV\tPSW, A");
    return true;
}

static bool trans_MOV_A_PSW(DisasContext *ctx, arg_MOV_A_PSW *a)
{
    print("MOV\tA, PSW");
    return true;
}

static bool trans_MOV_A_addr(DisasContext *ctx, arg_MOV_A_addr *a)
{
    print("MOV\tA, !0x%05x", rl78_word(a->addr) + 0xF0000);
    return true;
}

static bool trans_MOV_A_saddr(DisasContext *ctx, arg_MOV_A_saddr *a)
{
    const uint32_t saddr = a->saddr + 0xFFE20;

    print("MOV\tA, 0x%05x", saddr);

    return true;
}

static bool trans_MOV_saddr_A(DisasContext *ctx, arg_MOV_saddr_A *a)
{
    const uint32_t saddr = a->saddr + 0xFFE20;

    print("MOV\t0x%05x, A", saddr);

    return true;
}

static bool trans_MOV_ES_i(DisasContext *ctx, arg_MOV_ES_i *a)
{
    print("MOV\tES, #%d", a->imm);
    return true;
}

static bool trans_MOV_ES_saddr(DisasContext *ctx, arg_MOV_ES_saddr *a)
{
    print("MOV\tES, !0x%05x", rl78_gen_saddr(a->saddr));
    return true;
}

static bool trans_MOV_ES_A(DisasContext *ctx, arg_MOV_ES_A *a)
{
    print("MOV\tES, A");
    return true;
}

static bool trans_MOV_A_ES(DisasContext *ctx, arg_MOV_A_ES *a)
{
    print("MOV\tA, ES");
    return true;
}

static bool trans_MOV_CS_i(DisasContext *ctx, arg_MOV_CS_i *a)
{
    print("MOV\tCS, #%5d", a->imm);
    return true;
}

static bool trans_MOC_A_CS(DisasContext *ctx, arg_MOC_A_CS *a)
{
    print("MOV\tA, CS");
    return true;
}

static bool trans_MOV_CS_A(DisasContext *ctx, arg_MOV_CS_A *a)
{
    print("MOV\tCS, A");
    return true;
}

static bool trans_MOVW_rp_i(DisasContext *ctx, arg_MOVW_rp_i *a)
{
    const uint32_t imm = a->datal | (a->datah << 8);
    print("MOVW\tRP%d, #%d", a->rp*2, imm);
    return true;
}

static bool trans_CMP_A_i(DisasContext *ctx, arg_CMP_A_i *a)
{
    print("CMP\tA, #%d", a->imm);
    return true;
}

static bool trans_BR_addr16(DisasContext *ctx, arg_BR_addr16 *a)
{
    print("BR\t!0x%04x", rl78_word(a->addr));
    return true;
}

static bool trans_BNZ(DisasContext *ctx, arg_BNZ *a)
{
    print("BNZ\t$%d", (int8_t)a->addr);
    return true;
}

static bool trans_SKZ(DisasContext *ctx, arg_SKZ *a)
{
    print("SKZ");
    return true;
}



int print_insn_rl78(bfd_vma addr, disassemble_info *dis)
{
    DisasContext ctx;
    uint64_t insn;
    int i;

    ctx.dis = dis;
    ctx.pc = ctx.addr = addr;
    ctx.len = 0;

    insn = decode_load(&ctx);
    if (!decode(&ctx, insn)) {
        ctx.dis->fprintf_func(ctx.dis->stream, ".byte\t");
        for (i = 0; i < ctx.addr - addr; i++) {
            if (i > 0) {
                ctx.dis->fprintf_func(ctx.dis->stream, ",");
            }
            ctx.dis->fprintf_func(ctx.dis->stream, "0x%02x", (uint8_t)(insn >> 56));
            insn <<= 8;
        }
    }

    return ctx.addr - addr;
}
