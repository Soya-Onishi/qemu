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

static bool trans_MOV_A_indHL(DisasContext *ctx, arg_MOV_A_indHL *a)
{
    print("MOV\tA, [HL]");
    return true;
}

static bool trans_MOV_indHL_A(DisasContext *ctx, arg_MOV_indHL_A *a)
{
    print("MOV\t[HL], A");
    return true;
}

static bool trans_MOV_indHLoffset_i(DisasContext *ctx, arg_MOV_indHLoffset_i *a)
{
    print("MOV\t[HL+%d], #%d", a->offset, a->imm);
    return true;
}

static bool trans_MOV_A_indHLoffset(DisasContext *ctx, arg_MOV_A_indHLoffset *a)
{
    print("MOV\tA, [HL+%d]", a->offset);
    return true;
}

static bool trans_MOV_indHLoffset_A(DisasContext *ctx, arg_MOV_indHLoffset_A *a)
{
    print("MOV\t[HL+%d], A", a->offset);
    return true;
}

static bool trans_MOV_A_indHL_B(DisasContext *ctx, arg_MOV_A_indHL_B *a)
{
    print("MOV\tA, [HL+B]");
    return true;
}

static bool trans_MOV_indHL_B_A(DisasContext *ctx, arg_MOV_indHL_B_A *a)
{
    print("MOV\t[HL+B], A");
    return true;
}

static bool trans_MOV_A_indHL_C(DisasContext *ctx, arg_MOV_A_indHL_C *a)
{
    print("MOV\tA, [HL+C]");
    return true;
}

static bool trans_MOV_indHL_C_A(DisasContext *ctx, arg_MOV_indHL_C_A *a)
{
    print("MOV\t[HL+C], A");
    return true;
}

#define rl78_base(arg)  ((uint32_t)(arg->adrl) | (arg->adrh << 8))

static bool trans_MOV_indBbase_i(DisasContext *ctx, arg_MOV_indBbase_i *a)
{
    print("MOV\t%d[B], #%d", rl78_base(a), a->imm);
    return true;
}

static bool trans_MOV_A_indBbase(DisasContext *ctx, arg_MOV_A_indBbase *a)
{
    print("MOV\tA, %d[B]", rl78_base(a));
    return true;
}

static bool trans_MOV_indBbase_A(DisasContext *ctx, arg_MOV_indBbase_A *a)
{
    print("MOV\t%d[B], A", rl78_base(a));
    return true;
}

static bool trans_MOV_indCbase_i(DisasContext *ctx, arg_MOV_indCbase_i *a)
{
    print("MOV\t%d[C], #%d", rl78_base(a), a->imm);
    return true;
}

static bool trans_MOV_A_indCbase(DisasContext *ctx, arg_MOV_A_indCbase *a)
{
    print("MOV\tA, %d[C]", rl78_base(a));
    return true;
}

static bool trans_MOV_indCbase_A(DisasContext *ctx, arg_MOV_indCbase_A *a)
{
    print("MOV\t%d[C], A", rl78_base(a));
    return true;
}

static bool trans_MOV_indBCbase_i(DisasContext *ctx, arg_MOV_indBCbase_i *a)
{
    print("MOV\t%d[BC], #%d", rl78_base(a), a->imm);
    return true;
}

static bool trans_MOV_A_indBCbase(DisasContext *ctx, arg_MOV_A_indBCbase *a)
{
    print("MOV\tA, %d[BC]", rl78_base(a));
    return true;
}

static bool trans_MOV_indBCbase_A(DisasContext *ctx, arg_MOV_indBCbase_A *a)
{
    print("MOV\t%d[BC], A", rl78_base(a));
    return true;
}

static bool trans_MOV_indSPoffset_i(DisasContext *ctx, arg_MOV_indSPoffset_i *a)
{
    print("MOV\t[SP+%d], #%d", a->offset, a->imm);
    return true;
}

static bool trans_MOV_A_indSPoffset(DisasContext *ctx, arg_MOV_A_indSPoffset *a)
{
    print("MOV\tA, [SP+%d]", a->offset);
    return true;
}

static bool trans_MOV_indSPoffset_A(DisasContext *ctx, arg_MOV_indSPoffset_A *a)
{
    print("MOV\tA, [SP+%d], A", a->offset);
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

static bool trans_MOV_X_addr(DisasContext *ctx, arg_MOV_X_addr *a)
{
    const uint32_t addr = a->adrl | (a->adrh << 8);
    print("MOV\tX, !0x%05x", addr | 0xF0000);
    return true;
}

static bool trans_MOV_A_addr(DisasContext *ctx, arg_MOV_A_addr *a)
{
    const uint32_t addr = a->adrl | (a->adrh << 8);
    print("MOV\tA, !0x%05x", addr | 0xF0000);
    return true;
}

static bool trans_MOV_B_addr(DisasContext *ctx, arg_MOV_B_addr *a)
{
    const uint32_t addr = a->adrl | (a->adrh << 8);
    print("MOV\tB, !0x%05x", addr | 0xF0000);
    return true;
}

static bool trans_MOV_C_addr(DisasContext *ctx, arg_MOV_C_addr *a)
{
    const uint32_t addr = a->adrl | (a->adrh << 8);
    print("MOV\tC, !0x%05x", addr | 0xF0000);
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

static bool trans_XCH_A_X(DisasContext *ctx, arg_XCH_A_X *a)
{
    print("XCH\tA, X");
    return true;
}

static bool trans_XCH_A_C(DisasContext *ctx, arg_XCH_A_C *a)
{
    print("XCH\tA, C");
    return true;
}

static bool trans_XCH_A_B(DisasContext *ctx, arg_XCH_A_B *a)
{
    print("XCH\tA, B");
    return true;
}

static bool trans_XCH_A_E(DisasContext *ctx, arg_XCH_A_E *a)
{
    print("XCH\tA, E");
    return true;
}

static bool trans_XCH_A_D(DisasContext *ctx, arg_XCH_A_D *a)
{
    print("XCH\tA, D");
    return true;
}

static bool trans_XCH_A_L(DisasContext *ctx, arg_XCH_A_L *a)
{
    print("XCH\tA, L");
    return true;
}

static bool trans_XCH_A_H(DisasContext *ctx, arg_XCH_A_H *a)
{
    print("XCH\tA, H");
    return true;
}

static bool trans_XCH_A_addr(DisasContext *ctx, arg_XCH_A_addr *a)
{
    const uint32_t addr = a->adrl | (a->adrh << 8) | 0xF0000;
    print("XCH\tA, !0x%05x", addr);
    return true;
}

static bool trans_XCH_indDE(DisasContext *ctx, arg_XCH_indDE *a)
{
    print("XCH\tA, [DE]");
    return true;
}

static bool trans_XCH_indDEoffset(DisasContext *ctx, arg_XCH_indDEoffset *a)
{
    print("XCH\tA, [DE+%d]", a->offset);
    return true;
}

static bool trans_XCH_indHL(DisasContext *ctx, arg_XCH_indHL *a)
{
    print("XCH\tA, [HL]");
    return true;
}

static bool trans_XCH_indHLoffset(DisasContext *ctx, arg_XCH_indHLoffset *a)
{
    print("XCH\tA, [HL+%d]", a->offset);
    return true;
}

static bool trans_XCH_indHL_B(DisasContext *ctx, arg_XCH_indHL_B *a)
{
    print("XCH\tA, [HL+B]");
    return true;
}
static bool trans_XCH_indHL_C(DisasContext *ctx, arg_XCH_indHL_C *a)
{
    print("XCH\tA, [HL+C]");
    return true;
}

static bool trans_ONEB_r(DisasContext *ctx, arg_ONEB_r *a)
{
    print("ONEB\t%s", rl78_cpu_gp_regnames[a->r]);
    return true;
}

static bool trans_ONEB_addr(DisasContext *ctx, arg_ONEB_addr *a)
{
    print("ONEB\t!0x%05x", rl78_word(a->adrl | (a->adrh << 8)));
    return true;
}

static bool trans_CLRB_r(DisasContext *ctx, arg_CLRB_r *a)
{
    print("CLRB\t%s", rl78_cpu_gp_regnames[a->r]);
    return true;
}

static bool trans_CLRB_addr(DisasContext *ctx, arg_CLRB_addr *a)
{
    print("CLRB\t!0x%05x", rl78_word(a->adrl | (a->adrh << 8)));
    return true;
}

static bool trans_MOVS_indHLoffset_X(DisasContext *ctx, arg_MOVS_indHLoffset_X *a)
{
    print("MOVS\t[HL+%d], X", a->offset);
    return true;
}

static bool trans_MOVW_rp_i(DisasContext *ctx, arg_MOVW_rp_i *a)
{
    const uint32_t imm = a->datal | (a->datah << 8);
    print("MOVW\tRP%d, #%d", a->rp*2, imm);
    return true;
}

static bool trans_MOVW_SP_i(DisasContext *ctx, arg_MOVW_SP_i *a)
{
    const uint32_t imm = a->datal | (a->datah << 8);
    print("MOVW\tSP, #%d", imm);
    return true;
}

static bool trans_MOVW_addr_AX(DisasContext *ctx, arg_MOVW_addr_AX *a)
{
    const uint32_t addr =  (a->adrl |(a->adrh << 8)) | 0xF0000;
    print("MOVW\t!0x%05x, AX", addr);
    return true;
}

static bool trans_MOVW_AX_addr(DisasContext *ctx, arg_MOVW_AX_addr *a)
{
    const uint32_t addr =  (a->adrl |(a->adrh << 8)) | 0xF0000;
    print("MOVW\tAX, !0x%05x", addr);
    return true;
}

static bool trans_MOVW_BC_addr(DisasContext *ctx, arg_MOVW_BC_addr *a)
{
    const uint32_t addr = a->adrl | (a->adrh << 8) | 0xF0000;
    print("MOVW\tBC, !0x%05x", addr);
    return true;
}

static bool trans_MOVW_DE_addr(DisasContext *ctx, arg_MOVW_DE_addr *a)
{
    const uint32_t addr = a->adrl | (a->adrh << 8) | 0xF0000;
    print("MOVW\tDE, !0x%05x", addr);
    return true;
}

static bool trans_MOVW_HL_addr(DisasContext *ctx, arg_MOVW_HL_addr *a)
{
    const uint32_t addr = a->adrl | (a->adrh << 8) | 0xF0000;
    print("MOVW\tHL, !0x%05x", addr);
    return true;
}

static bool trans_MOVW_AX_indDE(DisasContext *ctx, arg_MOVW_AX_indDE *a)
{
    print("MOVW\tAX, [DE]");
    return true;
}

static bool trans_MOVW_indDE_AX(DisasContext *ctx, arg_MOVW_indDE_AX *a)
{
    print("MOVW\t[DE], AX");
    return true;
}

static bool trans_MOVW_AX_indDEoffset(DisasContext *ctx, arg_MOVW_AX_indDEoffset *a)
{
    print("MOVW\tAX, [DE+%d]", a->offset);
    return true;
}

static bool trans_MOVW_indDEoffset_AX(DisasContext *ctx, arg_MOVW_indDEoffset_AX *a)
{
    print("MOVW\t[DE+%d], AX", a->offset);
    return true;
}

static bool trans_MOVW_AX_indHL(DisasContext *ctx, arg_MOVW_AX_indHL *a)
{
    print("MOVW\tAX, [HL]");
    return true;
}

static bool trans_MOVW_indHL_AX(DisasContext *ctx, arg_MOVW_indHL_AX *a)
{
    print("MOVW\t[HL], AX");
    return true;
}

static bool trans_MOVW_AX_indHLoffset(DisasContext *ctx, arg_MOVW_AX_indHLoffset *a)
{
    print("MOVW\tAX, [HL+%d]", a->offset);
    return true;
}

static bool trans_MOVW_indHLoffset_AX(DisasContext *ctx, arg_MOVW_indHLoffset_AX *a)
{
    print("MOVW\t[HL+%d], AX", a->offset);
    return true;
}

static bool trans_MOVW_AX_indSPoffset(DisasContext *ctx, arg_MOVW_AX_indSPoffset *a)
{
    print("MOVW\tAX, [SP+%d]", a->offset);
    return true;
}

static bool trans_MOVW_indSPoffset_AX(DisasContext *ctx, arg_MOVW_indSPoffset_AX *a)
{
    print("MOVW\t[SP+%d], AX", a->offset);
    return true;
}

static bool trans_MOVW_AX_indBbase(DisasContext *ctx, arg_MOVW_AX_indBbase *a)
{
    const uint32_t base = a->adrl | (a->adrh << 8) | 0xF0000;
    print("MOVW\tAX, 0x%05x[B]", base);
    return true;
}

static bool trans_MOVW_indBbase_AX(DisasContext *ctx, arg_MOVW_indBbase_AX *a)
{
    const uint32_t base = a->adrl | (a->adrh << 8) | 0xF0000;
    print("MOVW\t0x%05x[B], AX", base);
    return true;
}

static bool trans_MOVW_AX_indCbase(DisasContext *ctx, arg_MOVW_AX_indCbase *a)
{
    const uint32_t base = a->adrl | (a->adrh << 8) | 0xF0000;
    print("MOVW\tAX, 0x%05x[C]", base);
    return true;
}

static bool trans_MOVW_indCbase_AX(DisasContext *ctx, arg_MOVW_indCbase_AX *a)
{
    const uint32_t base = a->adrl | (a->adrh << 8) | 0xF0000;
    print("MOVW\t0x%05x[C], AX", base);
    return true;
}

static bool trans_MOVW_AX_indBCbase(DisasContext *ctx, arg_MOVW_AX_indBCbase *a)
{
    const uint32_t base = a->adrl | (a->adrh << 8) | 0xF0000;
    print("MOVW\tAX, 0x%05x[BC]", base);
    return true;
}

static bool trans_MOVW_indBCbase_AX(DisasContext *ctx, arg_MOVW_indBCbase_AX *a)
{
    const uint32_t base = a->adrl | (a->adrh << 8) | 0xF0000;
    print("MOVW\t0x%05x[BC], AX", base);
    return true;
}

static bool trans_MOVW_AX_BC(DisasContext *ctx, arg_MOVW_AX_BC *a)
{
    print("MOVW\tAX, BC");
    return true;
}

static bool trans_MOVW_AX_DE(DisasContext *ctx, arg_MOVW_AX_DE *a)
{
    print("MOVW\tAX, DE");
    return true;
}

static bool trans_MOVW_AX_HL(DisasContext *ctx, arg_MOVW_AX_HL *a)
{
    print("MOVW\tAX, HL");
    return true;
}


static bool trans_MOVW_BC_AX(DisasContext *ctx, arg_MOVW_BC_AX *a)
{
    print("MOVW\tBC, AX");
    return true;
}

static bool trans_MOVW_DE_AX(DisasContext *ctx, arg_MOVW_DE_AX *a)
{
    print("MOVW\tDE, AX");
    return true;
}

static bool trans_MOVW_HL_AX(DisasContext *ctx, arg_MOVW_HL_AX *a)
{
    print("MOVW\tHL, AX");
    return true;
}

static bool trans_MOVW_SP_AX(DisasContext *ctx, arg_MOVW_SP_AX *a)
{
    print("MOVW\tSP, AX");
    return true;
}

static bool trans_MOVW_AX_SP(DisasContext *ctx, arg_MOVW_AX_SP *a)
{
    print("MOVW\tAX, SP");
    return true;
}

static bool trans_MOVW_BC_SP(DisasContext *ctx, arg_MOVW_BC_SP *a)
{
    print("MOVW\tBC, SP");
    return true;
}

static bool trans_MOVW_DE_SP(DisasContext *ctx, arg_MOVW_DE_SP *a)
{
    print("MOVW\tDE, SP");
    return true;
}

static bool trans_MOVW_HL_SP(DisasContext *ctx, arg_MOVW_HL_SP *a)
{
    print("MOVW\tHL, SP");
    return true;
}

static bool trans_XCHW_AX_BC(DisasContext *ctx, arg_XCHW_AX_BC *a)
{
    print("XCHW\tAX, BC");
    return true;
}

static bool trans_XCHW_AX_DE(DisasContext *ctx, arg_XCHW_AX_DE *a)
{
    print("XCHW\tAX, DE");
    return true;
}

static bool trans_XCHW_AX_HL(DisasContext *ctx, arg_XCHW_AX_HL *a)
{
    print("XCHW\tAX, HL");
    return true;
}

static bool trans_ONEW_AX(DisasContext *ctx, arg_ONEW_AX *a)
{
    print("ONEW\tAX");
    return true;
}

static bool trans_ONEW_BC(DisasContext *ctx, arg_ONEW_BC *a)
{
    print("ONEW\tBC");
    return true;
}

static bool trans_CLRW_AX(DisasContext *ctx, arg_CLRW_AX *a)
{
    print("CLRW\tAX");
    return true;
}

static bool trans_CLRW_BC(DisasContext *ctx, arg_CLRW_BC *a)
{
    print("CLRW\tBC");
    return true;
}

static const char* arith_op_name[] = {
    "ADD", "ADC", "SUB", "SBB", 
    "CMP", "AND", "OR", "XOR",
};

static bool trans_Arith_A_i(DisasContext *ctx, arg_Arith_A_i *a)
{
    print("%s\tA, #%d", arith_op_name[a->op], a->imm);
    return true;
}
static bool trans_Arith_A_X(DisasContext *ctx, arg_Arith_A_X *a)
{
    print("%s\tA, X", arith_op_name[a->op]);
    return true;
}

static bool trans_Arith_A_C(DisasContext *ctx, arg_Arith_A_C *a)
{
    print("%s\tA, C", arith_op_name[a->op]);
    return true;
}

static bool trans_Arith_A_B(DisasContext *ctx, arg_Arith_A_B *a)
{
    print("%s\tA, B", arith_op_name[a->op]);
    return true;
}

static bool trans_Arith_A_E(DisasContext *ctx, arg_Arith_A_E *a)
{
    print("%s\tA, E", arith_op_name[a->op]);
    return true;
}

static bool trans_Arith_A_D(DisasContext *ctx, arg_Arith_A_D *a)
{
    print("%s\tA, D", arith_op_name[a->op]);
    return true;
}

static bool trans_Arith_A_L(DisasContext *ctx, arg_Arith_A_L *a)
{
    print("%s\tA, L", arith_op_name[a->op]);
    return true;
}

static bool trans_Arith_A_H(DisasContext *ctx, arg_Arith_A_H *a)
{
    print("%s\tA, H", arith_op_name[a->op]);
    return true;
}

static bool trans_Arith_X_A(DisasContext *ctx, arg_Arith_X_A *a)
{
    print("%s\tX, A", arith_op_name[a->op]);
    return true;
}

static bool trans_Arith_A_A(DisasContext *ctx, arg_Arith_A_A *a)
{
    print("%s\tA, A", arith_op_name[a->op]);
    return true;
}

static bool trans_Arith_C_A(DisasContext *ctx, arg_Arith_C_A *a)
{
    print("%s\tC, A", arith_op_name[a->op]);
    return true;
}

static bool trans_Arith_B_A(DisasContext *ctx, arg_Arith_B_A *a)
{
    print("%s\tB, A", arith_op_name[a->op]);
    return true;
}
static bool trans_Arith_E_A(DisasContext *ctx, arg_Arith_E_A *a)
{
    print("%s\tE, A", arith_op_name[a->op]);
    return true;
}

static bool trans_Arith_D_A(DisasContext *ctx, arg_Arith_D_A *a)
{
    print("%s\tD, A", arith_op_name[a->op]);
    return true;
}

static bool trans_Arith_L_A(DisasContext *ctx, arg_Arith_L_A *a)
{
    print("%s\tL, A", arith_op_name[a->op]);
    return true;
}

static bool trans_Arith_H_A(DisasContext *ctx, arg_Arith_H_A *a)
{
    print("%s\tH, A", arith_op_name[a->op]);
    return true;
}

static bool trans_Arith_A_addr(DisasContext *ctx, arg_Arith_A_addr *a)
{
    const uint32_t addr = a->adrl | (a->adrh << 8) | 0xF0000;
    print("%s\tA, !0x%05x", arith_op_name[a->op], addr);
    return true;
}

static bool trans_Arith_A_indHL(DisasContext *ctx, arg_Arith_A_indHL *a)
{
    print("ADD\tA, [HL]");
    return true;
}

static bool trans_Arith_A_indHLoffset(DisasContext *ctx, arg_Arith_A_indHLoffset *a)
{
    print("ADD\tA, [HL+%d]", a->offset);
    return true;
}

static bool trans_Arith_A_indHL_B(DisasContext *ctx, arg_Arith_A_indHL_B *a)
{
    print("ADD\tA, [HL+B]");
    return true;
}

static bool trans_Arith_A_indHL_C(DisasContext *ctx, arg_Arith_A_indHL_C *a)
{
    print("ADD\tA, [HL+C]");
    return true;
}

static bool trans_CMP0_r(DisasContext *ctx, arg_CMP0_r *a)
{
    const char* reg_name[] = { "X", "A", "C", "B" };
    print("CMP0\t%s", reg_name[a->op]);
    return true;
}

static bool trans_CMP0_addr(DisasContext *ctx, arg_CMP0_addr *a)
{
    print("CMP0\t!0x%05x", rl78_word(a->adrl | (a->adrh << 8) | 0xF0000));
    return true;
}

static bool trans_CMPS_X_indHLoffset(DisasContext *ctx, arg_CMPS_X_indHLoffset *a)
{
    print("CMPS\tX, [HL+%d]", a->offset);
    return true;
}

static bool trans_ADDW_AX_i(DisasContext *ctx, arg_ADDW_AX_i *a)
{
    const uint32_t imm = a->datal | (a->datah << 8);
    print("ADDW\tAX, #%d", imm);
    return true;
}

static bool trans_ADDW_AX_rp(DisasContext *ctx, arg_ADDW_AX_rp *a)
{
    const char* rp_names[] = {
        "AX", "BC", "DE", "HL"
    };

    print("ADDW\tAX, %s", rp_names[a->rp]);
    return true;
}

static bool trans_ADDW_AX_addr(DisasContext *ctx, arg_ADDW_AX_addr *a)
{
    const uint32_t addr = a->adrl | (a->adrh << 8) | (0xF0000);
    print("ADDW\tAX, !%05x", addr);
    return true;
}

static bool trans_ADDW_AX_indHLoffset(DisasContext *ctx, arg_ADDW_AX_indHLoffset *a)
{
    print("ADDW\tAX, [HL+%d]", a->offset);
    return true;
}

static bool trans_SUBW_AX_i(DisasContext *ctx, arg_SUBW_AX_i *a)
{
    const uint32_t imm = a->datal | (a->datah << 8);
    print("SUBW\tAX, #%d", imm);
    return true;
}

static bool trans_SUBW_AX_rp(DisasContext *ctx, arg_SUBW_AX_rp *a)
{
    const char* rp_names[] = {
        "AX", "BC", "DE", "HL"
    };

    print("SUBW\tAX, %s", rp_names[a->rp]);
    return true;
}

static bool trans_SUBW_AX_addr(DisasContext *ctx, arg_SUBW_AX_addr *a)
{
    const uint32_t addr = a->adrl | (a->adrh << 8) | (0xF0000);
    print("SUBW\tAX, !%05x", addr);
    return true;
}

static bool trans_SUBW_AX_indHLoffset(DisasContext *ctx, arg_SUBW_AX_indHLoffset *a)
{
    print("SUBW\tAX, [HL+%d]", a->offset);
    return true;
}

static bool trans_CMPW_AX_i(DisasContext *ctx, arg_CMPW_AX_i *a)
{
    const uint32_t imm = a->datal | (a->datah << 8);
    print("CMPW\tAX, #%d", imm);
    return true;
}

static bool trans_CMPW_AX_BC(DisasContext *ctx, arg_CMPW_AX_BC *a)
{
    print("CMPW\tAX, BC");
    return true;
}

static bool trans_CMPW_AX_DE(DisasContext *ctx, arg_CMPW_AX_DE *a)
{
    print("CMPW\tAX, DE");
    return true;
}

static bool trans_CMPW_AX_HL(DisasContext *ctx, arg_CMPW_AX_HL *a)
{
    print("CMPW\tAX, HL");
    return true;
}

static bool trans_CMPW_AX_addr(DisasContext *ctx, arg_CMPW_AX_addr *a)
{
    const uint32_t addr = a->adrl | (a->adrh << 8) | (0xF0000);
    print("CMPW\tAX, !%05x", addr);
    return true;
}

static bool trans_CMPW_AX_indHLoffset(DisasContext *ctx, arg_CMPW_AX_indHLoffset *a)
{
    print("SUBW\tAX, [HL+%d]", a->offset);
    return true;
}

static bool trans_MULHU(DisasContext *ctx, arg_MULHU *a)
{
    print("MULHU");
    return true;
}

static bool trans_MULH(DisasContext *ctx, arg_MULH *a)
{
    print("MULH");
    return true;
}

static bool trans_DIVHU(DisasContext *ctx, arg_DIVHU *a)
{
    print("DIVHU");
    return true;
}

static bool trans_DIVWU(DisasContext *ctx, arg_DIVWU *a)
{
    print("DIVWU");
    return true;
}

static bool trans_MULU(DisasContext *ctx, arg_MULU *a)
{
    print("MULU\tX");
    return true;
}

static bool trans_INC_r(DisasContext *ctx, arg_INC_r *a)
{
    print("INC\t%s", rl78_cpu_gp_regnames[a->r]);
    return true;
}

static bool trans_INC_addr(DisasContext *ctx, arg_INC_addr *a)
{
    const uint32_t addr = a->adrl | (a->adrh << 8) | 0xF0000;
    print("INC\t!%05x", addr);
    return true;
}

static bool trans_INC_indHLoffset(DisasContext *ctx, arg_INC_indHLoffset *a)
{
    print("INC\t[HL+%d]", a->offset);
    return true;
}

static bool trans_DEC_r(DisasContext *ctx, arg_DEC_r *a)
{
    print("DEC\t%s", rl78_cpu_gp_regnames[a->r]);
    return true;
}

static bool trans_DEC_addr(DisasContext *ctx, arg_DEC_addr *a)
{
    const uint32_t addr = a->adrl | (a->adrh << 8) | 0xF0000;
    print("DEC\t!%05x", addr);
    return true;
}

static bool trans_DEC_indHLoffset(DisasContext *ctx, arg_DEC_indHLoffset *a)
{
    print("DEC\t[HL+%d]", a->offset);
    return true;
}

static bool trans_INCW_rp(DisasContext *ctx, arg_INCW_rp *a)
{
    const char* rpnames[] = {
        "AX", "BC", "DE", "HL"
    };

    print("INCW\t%s", rpnames[a->rp]);
    return true;
}

static bool trans_INCW_addr(DisasContext *ctx, arg_INCW_addr *a)
{
    const uint32_t addr = a->adrl | (a->adrh << 8) | 0xF0000;
    print("INCW\t!%05x", addr);
    return true;
}

static bool trans_INCW_indHLoffset(DisasContext *ctx, arg_INCW_indHLoffset *a)
{
    print("INCW\t[HL+%d]", a->offset);
    return true;
}

static bool trans_DECW_rp(DisasContext *ctx, arg_DECW_rp *a)
{
    const char* rpnames[] = {
        "AX", "BC", "DE", "HL"
    };

    print("DECW\t%s", rpnames[a->rp]);
    return true;
}

static bool trans_DECW_addr(DisasContext *ctx, arg_DECW_addr *a)
{
    const uint32_t addr = a->adrl | (a->adrh << 8) | 0xF0000;
    print("DECW\t!%05x", addr);
    return true;
}

static bool trans_DECW_indHLoffset(DisasContext *ctx, arg_DECW_indHLoffset *a)
{
    print("DECW\t[HL+%d]", a->offset);
    return true;
}

static bool trans_SAR_A_shamt(DisasContext *ctx, arg_SAR_A_shamt *a)
{
    print("SAR\tA, %d", a->shamt);
    return true;
}

static bool trans_SHR_A_shamt(DisasContext *ctx, arg_SHR_A_shamt *a)
{
    print("SHR\tA, %d", a->shamt);
    return true;
}

static bool trans_SHL_A_shamt(DisasContext *ctx, arg_SHL_A_shamt *a)
{
    print("SHL\tA, %d", a->shamt);
    return true;
}

static bool trans_SHL_B_shamt(DisasContext *ctx, arg_SHL_B_shamt *a)
{
    print("SHL\tB, %d", a->shamt);
    return true;
}

static bool trans_SHL_C_shamt(DisasContext *ctx, arg_SHL_C_shamt *a)
{
    print("SHL\tC, %d", a->shamt);
    return true;
}

static bool trans_SARW_AX_shamt(DisasContext *ctx, arg_SARW_AX_shamt *a)
{
    print("SARW\tAX, %d", a->shamt);
    return true;
}

static bool trans_SHRW_AX_shamt(DisasContext *ctx, arg_SHRW_AX_shamt *a)
{
    print("SHR\tAX, %d", a->shamt);
    return true;
}

static bool trans_SHLW_AX_shamt(DisasContext *ctx, arg_SHLW_AX_shamt *a)
{
    print("SHLW\tAX, %d", a->shamt);
    return true;
}

static bool trans_SHLW_BC_shamt(DisasContext *ctx, arg_SHLW_BC_shamt *a)
{
    print("SHLW\tBC, %d", a->shamt);
    return true;
}

static bool trans_ROR(DisasContext *ctx, arg_ROR *a)
{
    print("ROR\tA, 1");
    return true;
}

static bool trans_ROL(DisasContext *ctx, arg_ROL *a)
{
    print("ROL\tA, 1");
    return true;
}

static bool trans_RORC(DisasContext *ctx, arg_RORC *a)
{
    print("RORC\tA, 1");
    return true;
}

static bool trans_ROLC(DisasContext *ctx, arg_ROLC *a)
{
    print("ROLC\tA, 1");
    return true;
}

static bool trans_ROLWC_AX(DisasContext *ctx, arg_ROLWC_AX *a)
{
    print("ROLWC\tAX, 1");
    return true;
}

static bool trans_ROLWC_BC(DisasContext *ctx, arg_ROLWC_BC *a)
{
    print("ROLWC\tBC, 1");
    return true;
}

static bool trans_MOV1_CY_saddrbit(DisasContext *ctx, arg_MOV1_CY_saddrbit *a)
{
    const uint saddr = rl78_gen_saddr(a->saddr);
    print("MOV1\tCY, %05x.%d", saddr, a->bit);
    return true;
}

static bool trans_MOV1_CY_Abit(DisasContext *ctx, arg_MOV1_CY_Abit *a)
{
    print("MOV1\tCY, A.%d", a->bit);
    return true;
}

static bool trans_MOV1_CY_indHLbit(DisasContext *ctx, arg_MOV1_CY_indHLbit *a)
{
    print("MOV1\tCY, [HL].%d", a->bit);
    return true;  
}

static bool trans_MOV1_CY_PSWbit(DisasContext *ctx, arg_MOV1_CY_PSWbit *a)
{
    print("MOV1\tCY, PSW.%d", a->bit);
    return true;
}

static bool trans_MOV1_CY_sfrbit(DisasContext *ctx, arg_MOV1_CY_sfrbit *a)
{
    print("MOV1\tCY, 0x%02x.%d", a->sfr, a->bit);
    return true;
}

static bool trans_MOV1_saddrbit_CY(DisasContext *ctx, arg_MOV1_saddrbit_CY *a)
{
    const uint saddr = rl78_gen_saddr(a->saddr);
    print("MOV1\t%05x.%d, CY", saddr, a->bit);
    return true;
}

static bool trans_MOV1_Abit_CY(DisasContext *ctx, arg_MOV1_Abit_CY *a)
{
    print("MOV1\tA.%d, CY", a->bit);
    return true;
}

static bool trans_MOV1_indHLbit_CY(DisasContext *ctx, arg_MOV1_indHLbit_CY *a)
{
    print("MOV1\t[HL].%d, CY", a->bit);
    return true;
}

static bool trans_MOV1_PSWbit_CY(DisasContext *ctx, arg_MOV1_PSWbit_CY *a)
{
    print("MOV1\tPSW.%d, CY", a->bit);
    return true;
}

static bool trans_MOV1_sfrbit_CY(DisasContext *ctx, arg_MOV1_sfrbit_CY *a)
{
    print("MOV1\t0x%02x.%d, CY", a->sfr, a->bit);
    return true;
}

static bool trans_AND1_CY_saddr(DisasContext *ctx, arg_AND1_CY_saddr *a)
{
    const uint saddr = rl78_gen_saddr(a->saddr);
    print("AND1\tCY, %05x.%d", saddr, a->bit);
    return true;
}

static bool trans_AND1_CY_A(DisasContext *ctx, arg_AND1_CY_A *a)
{
    print("AND1\tCY, A.%d", a->bit);
    return true;
}

static bool trans_AND1_CY_indHL(DisasContext *ctx, arg_AND1_CY_indHL *a)
{
    print("AND1\tCY, [HL].%d", a->bit);
    return true;
}

static bool trans_AND1_CY_PSW(DisasContext *ctx, arg_AND1_CY_PSW *a)
{
    print("AND1\tCY, PSW.%d", a->bit);
    return true;
}

static bool trans_AND1_CY_sfr(DisasContext *ctx, arg_AND1_CY_sfr *a)
{
    print("AND1\tCY, 0x%02x.%d", a->sfr, a->bit);
    return true;
}

static bool trans_OR1_CY_saddr(DisasContext *ctx, arg_OR1_CY_saddr *a)
{
    const uint saddr = rl78_gen_saddr(a->saddr);
    print("OR1\tCY, %05x.%d", saddr, a->bit);
    return true;
}

static bool trans_OR1_CY_A(DisasContext *ctx, arg_OR1_CY_A *a)
{
    print("OR1\tCY, A.%d", a->bit);
    return true;
}

static bool trans_OR1_CY_indHL(DisasContext *ctx, arg_OR1_CY_indHL *a)
{
    print("OR1\tCY, [HL].%d", a->bit);
    return true;
}

static bool trans_OR1_CY_PSW(DisasContext *ctx, arg_OR1_CY_PSW *a)
{
    print("OR1\tCY, PSW.%d", a->bit);
    return true;
}

static bool trans_OR1_CY_sfr(DisasContext *ctx, arg_OR1_CY_sfr *a)
{
    print("OR1\tCY, 0x%02x.%d", a->sfr, a->bit);
    return true;
}

static bool trans_XOR1_CY_saddr(DisasContext *ctx, arg_XOR1_CY_saddr *a)
{
    const uint saddr = rl78_gen_saddr(a->saddr);
    print("XOR1\tCY, %05x.%d", saddr, a->bit);
    return true;
}

static bool trans_XOR1_CY_A(DisasContext *ctx, arg_XOR1_CY_A *a)
{
    print("XOR1\tCY, A.%d", a->bit);
    return true;
}

static bool trans_XOR1_CY_indHL(DisasContext *ctx, arg_XOR1_CY_indHL *a)
{
    print("XOR1\tCY, [HL].%d", a->bit);
    return true;
}

static bool trans_XOR1_CY_PSW(DisasContext *ctx, arg_XOR1_CY_PSW *a)
{
    print("XOR1\tCY, PSW.%d", a->bit);
    return true;
}

static bool trans_XOR1_CY_sfr(DisasContext *ctx, arg_XOR1_CY_sfr *a)
{
    print("XOR1\tCY, 0x%02x.%d", a->sfr, a->bit);
    return true;
}

static bool trans_SET1_CY(DisasContext *ctx, arg_SET1_CY *a)
{
    print("SET1\tCY");
    return true;
}

static bool trans_SET1_saddr(DisasContext *ctx, arg_SET1_saddr *a)
{
    const uint saddr = rl78_gen_saddr(a->saddr);
    print("SET1\t%05x.%d", saddr, a->bit);
    return true;
}

static bool trans_SET1_A(DisasContext *ctx, arg_SET1_A *a)
{
    print("SET1\tA.%d", a->bit);
    return true;
}

static bool trans_SET1_indHL(DisasContext *ctx, arg_SET1_indHL *a)
{
    print("SET1\t[HL].%d", a->bit);
    return true;
}

static bool trans_SET1_PSW(DisasContext *ctx, arg_SET1_PSW *a)
{
    print("SET1\tPSW.%d", a->bit);
    return true;
}

static bool trans_SET1_sfr(DisasContext *ctx, arg_SET1_sfr *a)
{
    print("SET1\t0x%02x.%d", a->sfr, a->bit);
    return true;
}

static bool trans_SET1_addr(DisasContext *ctx, arg_SET1_addr *a)
{
    const uint addr = a->adrl | (a->adrh << 8) | 0xF0000;
    print("SET1\t!0x%04x.%d", addr, a->bit);
    return true;
}

static bool trans_CLR1_CY(DisasContext *ctx, arg_CLR1_CY *a)
{
    print("CLR1\tCY");
    return true;
}

static bool trans_CLR1_saddr(DisasContext *ctx, arg_CLR1_saddr *a)
{
    const uint saddr = rl78_gen_saddr(a->saddr);
    print("CLR1\t%05x.%d", saddr, a->bit);
    return true;
}

static bool trans_CLR1_A(DisasContext *ctx, arg_CLR1_A *a)
{
    print("CLR1\tA.%d", a->bit);
    return true;
}

static bool trans_CLR1_indHL(DisasContext *ctx, arg_CLR1_indHL *a)
{
    print("CLR1\t[HL].%d", a->bit);
    return true;
}

static bool trans_CLR1_sfr(DisasContext *ctx, arg_CLR1_sfr *a)
{
    print("CLR1\t0x%02x.%d", a->sfr, a->bit);
    return true;
}

static bool trans_CLR1_PSW(DisasContext *ctx, arg_CLR1_PSW *a)
{
    print("CLR1\tPSW.%d", a->bit);
    return true;
}

static bool trans_CLR1_addr(DisasContext *ctx, arg_CLR1_addr *a)
{
    const uint addr = a->adrl | (a->adrh << 8) | 0xF0000;
    print("CLR1\t!0x%04x.%d", addr, a->bit);
    return true;
}

static bool trans_NOT1_CY(DisasContext *ctx, arg_NOT1_CY *a)
{
    print("NOT1\tCY");
    return true;
}

static bool trans_CALL_rp(DisasContext *ctx, arg_CALL_rp *a)
{
    const char* rpnames[] = {
        "AX", "BC", "DE", "HL"
    };

    print("CALL\t%s", rpnames[a->rp]);
    return true;
}

static bool trans_CALL_addr20rel(DisasContext *ctx, arg_CALL_addr20rel *a)
{
    const int16_t rel = (int16_t)(a->adrl | (a->adrh << 8));
    print("CALL\t$!%d", rel);
    return true;
}

static bool trans_CALL_addr16(DisasContext *ctx, arg_CALL_addr16 *a)
{
    const uint addr = a->adrl | (a->adrh << 8);
    print("CALL\t!%04x", addr);
    return true;
}

static bool trans_CALL_addr20abs(DisasContext *ctx, arg_CALL_addr20abs *a)
{
    const uint addr = a->adrl | (a->adrh << 8) | (a->adrs << 16);
    print("CALL\t!%05x", addr);
    return true;
}

static bool trans_CALLT(DisasContext *ctx, arg_CALLT *a)
{
    const uint idx = a->idxl | (a->idxh << 3);
    const uint addr = 0x80 | (idx << 1);

    print("CALLT\t[0x%04x]", addr);
    return true;
}

static bool trans_RET(DisasContext *ctx, arg_RET *a)
{
    print("RET");
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
