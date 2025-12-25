#include "qemu/osdep.h"
#include "disas/dis-asm.h"
#include "qemu/bitops.h"
#include "cpu.h"

typedef struct DisasContext {
    disassemble_info *dis;

    uint32_t addr;
    uint32_t pc;

    uint8_t len;
    uint8_t bytes[5];
} DisasContext;

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

#define print(...)                                             \
    do {                                                            \
        dump_bytes(ctx);                                            \
        ctx->dis->fprintf_func(ctx->dis->stream, __VA_ARGS__); \
    } while(0)

#include "decode-insn.c.inc"

static bool trans_MOV_ri(DisasContext *ctx, arg_MOV_ri *a)
{
    print("MOV\tR%d, #%d", a->rd, a->imm);
    return true;
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
    print("MOV\t!0x%05x, #%d", a->addr + 0xF0000, a->imm);
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

static bool trans_BR_addr16(DisasContext *ctx, arg_BR_addr16 *a)
{
    print("BR\t!0x%04x", a->addr);
    return true;
}

static bool trans_BNZ(DisasContext *ctx, arg_BNZ *a)
{
    print("BNZ\t$%d", (int8_t)a->addr);
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
