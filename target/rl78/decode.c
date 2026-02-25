#include "decode.h"
#include "cpu.h"
#include "qemu/compiler.h"
#include "qemu/osdep.h"

struct DisasContext;
typedef struct DisasContext DisasContext;

#define ENTRY(insn, op0, op1)                                                  \
    {                                                                          \
        .mnemonic = glue(RL78_INSN_, insn),                                    \
        .operand  = {                                                          \
            op0,                                                              \
            op1,                                                              \
        },                                                                    \
    }

#define WREG(reg)                                                              \
    {                                                                          \
        .kind = RL78_OP_WORD_REG, .word_reg = RL78_WORD_REG_##reg,             \
    }

#define BREG(reg)                                                              \
    {                                                                          \
        .kind = RL78_OP_BYTE_REG, .byte_reg = RL78_BYTE_REG_##reg,             \
    }

#define IND_RR(base_reg, idx_reg)                                              \
    {                                                                          \
        .kind        = RL78_OP_IND_REG_REG,                                    \
        .ind_reg_reg = {                                                       \
            .base = RL78_WORD_REG_##base_reg,                                  \
            .idx  = RL78_BYTE_REG_##idx_reg,                                   \
        },                                                                     \
    }

#define IND_RI(base_reg)                                                       \
    {                                                                          \
        .kind        = RL78_OP_IND_REG_IMM,                                    \
        .ind_reg_imm = {                                                       \
            .base = RL78_WORD_REG_##base_reg,                                  \
            .imm  = 0,                                                         \
        },                                                                     \
    }

#define IND_BB(reg)                                                            \
    {                                                                          \
        .kind          = RL78_OP_IND_BASE_BYTE,                                \
        .ind_base_byte = {                                                     \
            .base = 0,                                                         \
            .idx  = RL78_BYTE_REG_##reg,                                       \
        },                                                                     \
    }

#define IND_BW(reg)                                                            \
    {                                                                          \
        .kind          = RL78_OP_IND_BASE_WORD,                                \
        .ind_base_word = {                                                     \
            .base = 0,                                                         \
            .idx  = RL78_WORD_REG_##reg,                                       \
        },                                                                     \
    }

#define SHAMT(n)                                                               \
    {                                                                          \
        .kind = RL78_OP_SHAMT, .const_op = n,                                  \
    }

#define CALLT_IDX(idx)                                                         \
    {                                                                          \
        .kind = RL78_OP_CALLT, .const_op = idx,                                \
    }

#define SEL_RB(idx)                                                            \
    {                                                                          \
        .kind = RL78_OP_SEL_RB, .const_op = idx,                               \
    }

#define BIT_OP(op_kind, bit_idx)                                               \
    {                                                                          \
        .kind = RL78_OP_BIT, .bit = {                                          \
            .kind = RL78_BITOP_##op_kind,                                      \
            .bit  = bit_idx,                                                   \
            .addr = 0,                                                         \
        }                                                                      \
    }

#define op(operand)                                                            \
    {                                                                          \
        .kind = glue(RL78_OP_, operand), .const_op = 0,                        \
    }

#define op_imm8_val(n)                                                         \
    {                                                                          \
        .kind = RL78_OP_IMM8, .imm = (n)                                       \
    }

static const RL78Instruction decode_table_1st_map[256] = {
    /* Row 0 (0x0X) */
    [0x00] = ENTRY(NOP, op(NONE), op(NONE)),
    [0x01] = ENTRY(ADDW, WREG(AX), WREG(AX)),
    [0x02] = ENTRY(ADDW, WREG(AX), op(ABS16)),
    [0x03] = ENTRY(ADDW, WREG(AX), WREG(BC)),
    [0x04] = ENTRY(ADDW, WREG(AX), op(IMM16)),
    [0x05] = ENTRY(ADDW, WREG(AX), WREG(DE)),
    [0x06] = ENTRY(ADDW, WREG(AX), op(SADDR)),
    [0x07] = ENTRY(ADDW, WREG(AX), WREG(HL)),
    [0x08] = ENTRY(XCH, BREG(A), BREG(X)),
    [0x09] = ENTRY(MOV, BREG(A), IND_BB(B)),
    [0x0a] = ENTRY(ADD, op(SADDR), op(IMM8)),
    [0x0b] = ENTRY(ADD, BREG(A), op(SADDR)),
    [0x0c] = ENTRY(ADD, BREG(A), op(IMM8)),
    [0x0d] = ENTRY(ADD, BREG(A), IND_RI(HL)),
    [0x0e] = ENTRY(ADD, BREG(A), op(IND_REG_IMM)),
    [0x0f] = ENTRY(ADD, BREG(A), op(ABS16)),

    /* Row 1 (0x1X) */
    [0x10] = ENTRY(ADDW, op(SP), op(IMM8)),
    [0x11] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* PREFIX */
    [0x12] = ENTRY(MOVW, WREG(BC), WREG(AX)),
    [0x13] = ENTRY(MOVW, WREG(AX), WREG(BC)),
    [0x14] = ENTRY(MOVW, WREG(DE), WREG(AX)),
    [0x15] = ENTRY(MOVW, WREG(AX), WREG(DE)),
    [0x16] = ENTRY(MOVW, WREG(HL), WREG(AX)),
    [0x17] = ENTRY(MOVW, WREG(AX), WREG(HL)),
    [0x18] = ENTRY(MOV, IND_BB(B), BREG(A)),
    [0x19] = ENTRY(MOV, IND_BB(B), op(IMM8)),
    [0x1a] = ENTRY(ADDC, op(SADDR), op(IMM8)),
    [0x1b] = ENTRY(ADDC, BREG(A), op(SADDR)),
    [0x1c] = ENTRY(ADDC, BREG(A), op(IMM8)),
    [0x1d] = ENTRY(ADDC, BREG(A), IND_RI(HL)),
    [0x1e] = ENTRY(ADDC, BREG(A), op(IND_REG_IMM)),
    [0x1f] = ENTRY(ADDC, BREG(A), op(ABS16)),

    /* Row 2 (0x2X) */
    [0x20] = ENTRY(SUBW, op(SP), op(IMM8)),
    [0x21] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x22] = ENTRY(SUBW, WREG(AX), op(ABS16)),
    [0x23] = ENTRY(SUBW, WREG(AX), WREG(BC)),
    [0x24] = ENTRY(SUBW, WREG(AX), op(IMM16)),
    [0x25] = ENTRY(SUBW, WREG(AX), WREG(DE)),
    [0x26] = ENTRY(SUBW, WREG(AX), op(SADDR)),
    [0x27] = ENTRY(SUBW, WREG(AX), WREG(HL)),
    [0x28] = ENTRY(MOV, IND_BB(C), BREG(A)),
    [0x29] = ENTRY(MOV, BREG(A), IND_BB(C)),
    [0x2a] = ENTRY(SUB, op(SADDR), op(IMM8)),
    [0x2b] = ENTRY(SUB, BREG(A), op(SADDR)),
    [0x2c] = ENTRY(SUB, BREG(A), op(IMM8)),
    [0x2d] = ENTRY(SUB, BREG(A), IND_RI(HL)),
    [0x2e] = ENTRY(SUB, BREG(A), op(IND_REG_IMM)),
    [0x2f] = ENTRY(SUB, BREG(A), op(ABS16)),

    /* Row 3 (0x3X) */
    [0x30] = ENTRY(MOVW, WREG(AX), op(IMM16)),
    [0x31] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 4th MAP */
    [0x32] = ENTRY(MOVW, WREG(BC), op(IMM16)),
    [0x33] = ENTRY(XCHW, WREG(AX), WREG(BC)),
    [0x34] = ENTRY(MOVW, WREG(DE), op(IMM16)),
    [0x35] = ENTRY(XCHW, WREG(AX), WREG(DE)),
    [0x36] = ENTRY(MOVW, WREG(HL), op(IMM16)),
    [0x37] = ENTRY(XCHW, WREG(AX), WREG(HL)),
    [0x38] = ENTRY(MOV, IND_BB(C), op(IMM8)),
    [0x39] = ENTRY(MOV, IND_BW(BC), op(IMM16)),
    [0x3a] = ENTRY(SUBC, op(SADDR), op(IMM8)),
    [0x3b] = ENTRY(SUBC, BREG(A), op(SADDR)),
    [0x3c] = ENTRY(SUBC, BREG(A), op(IMM8)),
    [0x3d] = ENTRY(SUBC, BREG(A), IND_RI(HL)),
    [0x3e] = ENTRY(SUBC, BREG(A), op(IND_REG_IMM)),
    [0x3f] = ENTRY(SUBC, BREG(A), op(ABS16)),

    /* Row 4 (0x4X) */
    [0x40] = ENTRY(CMP, op(ABS16), op(IMM8)),
    [0x41] = ENTRY(MOV, op(ES), op(IMM8)),
    [0x42] = ENTRY(CMPW, WREG(AX), op(ABS16)),
    [0x43] = ENTRY(CMPW, WREG(AX), WREG(BC)),
    [0x44] = ENTRY(CMPW, WREG(AX), op(IMM16)),
    [0x45] = ENTRY(CMPW, WREG(AX), WREG(DE)),
    [0x46] = ENTRY(CMPW, WREG(AX), op(SADDR)),
    [0x47] = ENTRY(CMPW, WREG(AX), WREG(HL)),
    [0x48] = ENTRY(MOV, IND_BW(BC), BREG(A)),
    [0x49] = ENTRY(MOV, BREG(A), IND_BW(BC)),
    [0x4a] = ENTRY(CMP, op(SADDR), op(IMM8)),
    [0x4b] = ENTRY(CMP, BREG(A), op(SADDR)),
    [0x4c] = ENTRY(CMP, BREG(A), op(IMM8)),
    [0x4d] = ENTRY(CMP, BREG(A), IND_RI(HL)),
    [0x4e] = ENTRY(CMP, BREG(A), op(IND_REG_IMM)),
    [0x4f] = ENTRY(CMP, BREG(A), op(ABS16)),

    /* Row 5 (0x5X) */
    [0x50] = ENTRY(MOV, BREG(X), op(IMM8)),
    [0x51] = ENTRY(MOV, BREG(A), op(IMM8)),
    [0x52] = ENTRY(MOV, BREG(C), op(IMM8)),
    [0x53] = ENTRY(MOV, BREG(B), op(IMM8)),
    [0x54] = ENTRY(MOV, BREG(E), op(IMM8)),
    [0x55] = ENTRY(MOV, BREG(D), op(IMM8)),
    [0x56] = ENTRY(MOV, BREG(L), op(IMM8)),
    [0x57] = ENTRY(MOV, BREG(H), op(IMM8)),
    [0x58] = ENTRY(MOVW, IND_BB(B), WREG(AX)),
    [0x59] = ENTRY(MOVW, WREG(AX), IND_BB(B)),
    [0x5a] = ENTRY(AND, op(SADDR), op(IMM8)),
    [0x5b] = ENTRY(AND, BREG(A), op(SADDR)),
    [0x5c] = ENTRY(AND, BREG(A), op(IMM8)),
    [0x5d] = ENTRY(AND, BREG(A), IND_RI(HL)),
    [0x5e] = ENTRY(AND, BREG(A), op(IND_REG_IMM)),
    [0x5f] = ENTRY(AND, BREG(A), op(ABS16)),

    /* Row 6 (0x6X) */
    [0x60] = ENTRY(MOV, BREG(A), BREG(X)),
    [0x61] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 2nd MAP */
    [0x62] = ENTRY(MOV, BREG(A), BREG(C)),
    [0x63] = ENTRY(MOV, BREG(A), BREG(B)),
    [0x64] = ENTRY(MOV, BREG(A), BREG(E)),
    [0x65] = ENTRY(MOV, BREG(A), BREG(D)),
    [0x66] = ENTRY(MOV, BREG(A), BREG(L)),
    [0x67] = ENTRY(MOV, BREG(A), BREG(H)),
    [0x68] = ENTRY(MOVW, IND_BB(C), WREG(AX)),
    [0x69] = ENTRY(MOVW, WREG(AX), IND_BB(C)),
    [0x6a] = ENTRY(OR, op(SADDR), op(IMM8)),
    [0x6b] = ENTRY(OR, BREG(A), op(SADDR)),
    [0x6c] = ENTRY(OR, BREG(A), op(IMM8)),
    [0x6d] = ENTRY(OR, BREG(A), IND_RI(HL)),
    [0x6e] = ENTRY(OR, BREG(A), op(IND_REG_IMM)),
    [0x6f] = ENTRY(OR, BREG(A), op(ABS16)),

    /* Row 7 (0x7X) */
    [0x70] = ENTRY(MOV, BREG(X), BREG(A)),
    [0x71] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 3rd MAP */
    [0x72] = ENTRY(MOV, BREG(C), BREG(A)),
    [0x73] = ENTRY(MOV, BREG(B), BREG(A)),
    [0x74] = ENTRY(MOV, BREG(E), BREG(A)),
    [0x75] = ENTRY(MOV, BREG(D), BREG(A)),
    [0x76] = ENTRY(MOV, BREG(L), BREG(A)),
    [0x77] = ENTRY(MOV, BREG(H), BREG(A)),
    [0x78] = ENTRY(MOVW, IND_BW(BC), WREG(AX)),
    [0x79] = ENTRY(MOVW, WREG(AX), IND_BW(BC)),
    [0x7a] = ENTRY(XOR, op(SADDR), op(IMM8)),
    [0x7b] = ENTRY(XOR, BREG(A), op(SADDR)),
    [0x7c] = ENTRY(XOR, BREG(A), op(IMM8)),
    [0x7d] = ENTRY(XOR, BREG(A), IND_RI(HL)),
    [0x7e] = ENTRY(XOR, BREG(A), op(IND_REG_IMM)),
    [0x7f] = ENTRY(XOR, BREG(A), op(ABS16)),

    /* Row 8 (0x8X) */
    [0x80] = ENTRY(INC, BREG(X), op(NONE)),
    [0x81] = ENTRY(INC, BREG(A), op(NONE)),
    [0x82] = ENTRY(INC, BREG(C), op(NONE)),
    [0x83] = ENTRY(INC, BREG(B), op(NONE)),
    [0x84] = ENTRY(INC, BREG(E), op(NONE)),
    [0x85] = ENTRY(INC, BREG(D), op(NONE)),
    [0x86] = ENTRY(INC, BREG(L), op(NONE)),
    [0x87] = ENTRY(INC, BREG(H), op(NONE)),
    [0x88] = ENTRY(MOV, BREG(A), op(IND_SP_IMM)),
    [0x89] = ENTRY(MOV, BREG(A), IND_RI(DE)),
    [0x8a] = ENTRY(MOV, BREG(A), IND_RI(DE)),
    [0x8b] = ENTRY(MOV, BREG(A), IND_RI(HL)),
    [0x8c] = ENTRY(MOV, BREG(A), IND_RI(HL)),
    [0x8d] = ENTRY(MOV, BREG(A), op(SADDR)),
    [0x8e] = ENTRY(MOV, BREG(A), op(SFR)),
    [0x8f] = ENTRY(MOV, BREG(A), op(ABS16)),

    /* Row 9 (0x9X) */
    [0x90] = ENTRY(DEC, BREG(X), op(NONE)),
    [0x91] = ENTRY(DEC, BREG(A), op(NONE)),
    [0x92] = ENTRY(DEC, BREG(C), op(NONE)),
    [0x93] = ENTRY(DEC, BREG(B), op(NONE)),
    [0x94] = ENTRY(DEC, BREG(E), op(NONE)),
    [0x95] = ENTRY(DEC, BREG(D), op(NONE)),
    [0x96] = ENTRY(DEC, BREG(L), op(NONE)),
    [0x97] = ENTRY(DEC, BREG(H), op(NONE)),
    [0x98] = ENTRY(MOV, op(IND_SP_IMM), BREG(A)),
    [0x99] = ENTRY(MOV, IND_RI(DE), BREG(A)),
    [0x9a] = ENTRY(MOV, IND_RI(DE), BREG(A)),
    [0x9b] = ENTRY(MOV, IND_RI(HL), BREG(A)),
    [0x9c] = ENTRY(MOV, IND_RI(HL), BREG(A)),
    [0x9d] = ENTRY(MOV, op(SADDR), BREG(A)),
    [0x9e] = ENTRY(MOV, op(SFR), BREG(A)),
    [0x9f] = ENTRY(MOV, op(ABS16), BREG(A)),

    /* Row A (0xAX) */
    [0xa0] = ENTRY(INCW, op(ABS16), op(NONE)),
    [0xa1] = ENTRY(INCW, WREG(AX), op(NONE)),
    [0xa2] = ENTRY(INCW, op(ABS16), op(NONE)),
    [0xa3] = ENTRY(INCW, WREG(BC), op(NONE)),
    [0xa4] = ENTRY(INC, op(SADDR), op(NONE)),
    [0xa5] = ENTRY(INCW, WREG(DE), op(NONE)),
    [0xa6] = ENTRY(INCW, op(SADDR), op(NONE)),
    [0xa7] = ENTRY(INCW, WREG(HL), op(NONE)),
    [0xa8] = ENTRY(MOVW, WREG(AX), op(IND_SP_IMM)),
    [0xa9] = ENTRY(MOVW, WREG(AX), IND_RI(DE)),
    [0xaa] = ENTRY(MOVW, WREG(AX), op(IND_REG_IMM)),
    [0xab] = ENTRY(MOVW, WREG(AX), IND_RI(HL)),
    [0xac] = ENTRY(MOVW, WREG(AX), op(IND_REG_IMM)),
    [0xad] = ENTRY(MOVW, WREG(AX), op(SADDR)),
    [0xae] = ENTRY(MOVW, WREG(AX), op(SFR)),
    [0xaf] = ENTRY(MOVW, WREG(AX), op(ABS16)),

    /* Row B (0xBX) */
    [0xb0] = ENTRY(DECW, op(ABS16), op(NONE)),
    [0xb1] = ENTRY(DECW, WREG(AX), op(NONE)),
    [0xb2] = ENTRY(DECW, op(ABS16), op(NONE)),
    [0xb3] = ENTRY(DECW, WREG(BC), op(NONE)),
    [0xb4] = ENTRY(DEC, op(SADDR), op(NONE)),
    [0xb5] = ENTRY(DECW, WREG(DE), op(NONE)),
    [0xb6] = ENTRY(DECW, op(SADDR), op(NONE)),
    [0xb7] = ENTRY(DECW, WREG(HL), op(NONE)),
    [0xb8] = ENTRY(MOVW, op(IND_SP_IMM), WREG(AX)),
    [0xb9] = ENTRY(MOVW, IND_RI(DE), WREG(AX)),
    [0xba] = ENTRY(MOVW, op(IND_REG_IMM), WREG(AX)),
    [0xbb] = ENTRY(MOVW, IND_RI(HL), WREG(AX)),
    [0xbc] = ENTRY(MOVW, op(IND_REG_IMM), WREG(AX)),
    [0xbd] = ENTRY(MOVW, op(SADDR), WREG(AX)),
    [0xbe] = ENTRY(MOVW, op(SFR), WREG(AX)),
    [0xbf] = ENTRY(MOVW, op(ABS16), WREG(AX)),

    /* Row C (0xCX) */
    [0xc0] = ENTRY(POP, WREG(AX), op(NONE)),
    [0xc1] = ENTRY(PUSH, WREG(AX), op(NONE)),
    [0xc2] = ENTRY(POP, WREG(BC), op(NONE)),
    [0xc3] = ENTRY(PUSH, WREG(BC), op(NONE)),
    [0xc4] = ENTRY(POP, WREG(DE), op(NONE)),
    [0xc5] = ENTRY(PUSH, WREG(DE), op(NONE)),
    [0xc6] = ENTRY(POP, WREG(HL), op(NONE)),
    [0xc7] = ENTRY(PUSH, WREG(HL), op(NONE)),
    [0xc8] = ENTRY(MOV, op(IND_SP_IMM), op(IMM8)),
    [0xc9] = ENTRY(MOVW, op(SADDR), op(IMM16)),
    [0xca] = ENTRY(MOV, IND_RI(DE), op(IMM8)),
    [0xcb] = ENTRY(MOVW, op(SFR), op(IMM16)),
    [0xcc] = ENTRY(MOV, IND_RI(HL), op(IMM8)),
    [0xcd] = ENTRY(MOV, op(SADDR), op(IMM8)),
    [0xce] = ENTRY(MOV, op(SFR), op(IMM8)),
    [0xcf] = ENTRY(MOV, op(ABS16), op(IMM8)),

    /* Row D (0xDX) */
    [0xd0] = ENTRY(CMP0, BREG(X), op(NONE)),
    [0xd1] = ENTRY(CMP0, BREG(A), op(NONE)),
    [0xd2] = ENTRY(CMP0, BREG(C), op(NONE)),
    [0xd3] = ENTRY(CMP0, BREG(B), op(NONE)),
    [0xd4] = ENTRY(CMP0, op(SADDR), op(NONE)),
    [0xd5] = ENTRY(CMP0, op(ABS16), op(NONE)),
    [0xd6] = ENTRY(MULU, BREG(X), op(NONE)),
    [0xd7] = ENTRY(RET, op(NONE), op(NONE)),
    [0xd8] = ENTRY(MOV, BREG(X), op(SADDR)),
    [0xd9] = ENTRY(MOV, BREG(X), op(ABS16)),
    [0xda] = ENTRY(MOVW, WREG(BC), op(SADDR)),
    [0xdb] = ENTRY(MOVW, WREG(BC), op(ABS16)),
    [0xdc] = ENTRY(BZ, op(REL8), op(NONE)),
    [0xdd] = ENTRY(BNC, op(REL8), op(NONE)),
    [0xde] = ENTRY(BNZ, op(REL8), op(NONE)),
    [0xdf] = ENTRY(BR, op(REL8), op(NONE)),

    /* Row E (0xEX) */
    [0xe0] = ENTRY(ONEB, BREG(X), op(NONE)),
    [0xe1] = ENTRY(ONEB, BREG(A), op(NONE)),
    [0xe2] = ENTRY(ONEB, BREG(C), op(NONE)),
    [0xe3] = ENTRY(ONEB, BREG(B), op(NONE)),
    [0xe4] = ENTRY(ONEB, op(SADDR), op(NONE)),
    [0xe5] = ENTRY(ONEB, op(ABS16), op(NONE)),
    [0xe6] = ENTRY(ONEW, WREG(AX), op(NONE)),
    [0xe7] = ENTRY(ONEW, WREG(BC), op(NONE)),
    [0xe8] = ENTRY(MOV, BREG(B), op(SADDR)),
    [0xe9] = ENTRY(MOV, BREG(B), op(ABS16)),
    [0xea] = ENTRY(MOVW, WREG(DE), op(SADDR)),
    [0xeb] = ENTRY(MOVW, WREG(DE), op(ABS16)),
    [0xec] = ENTRY(BR, op(ABS20), op(NONE)),
    [0xed] = ENTRY(BR, op(ABS16), op(NONE)),
    [0xee] = ENTRY(BR, op(REL16), op(NONE)),
    [0xef] = ENTRY(BR, op(REL8), op(NONE)),

    /* Row F (0xFX) */
    [0xf0] = ENTRY(CLRB, BREG(X), op(NONE)),
    [0xf1] = ENTRY(CLRB, BREG(A), op(NONE)),
    [0xf2] = ENTRY(CLRB, BREG(C), op(NONE)),
    [0xf3] = ENTRY(CLRB, BREG(B), op(NONE)),
    [0xf4] = ENTRY(CLRB, op(SADDR), op(NONE)),
    [0xf5] = ENTRY(CLRB, op(ABS16), op(NONE)),
    [0xf6] = ENTRY(CLRW, WREG(AX), op(NONE)),
    [0xf7] = ENTRY(CLRW, WREG(BC), op(NONE)),
    [0xf8] = ENTRY(MOV, BREG(C), op(SADDR)),
    [0xf9] = ENTRY(MOV, BREG(C), op(ABS16)),
    [0xfa] = ENTRY(MOVW, WREG(HL), op(SADDR)),
    [0xfb] = ENTRY(MOVW, WREG(HL), op(ABS16)),
    [0xfc] = ENTRY(CALL, op(ABS20), op(NONE)),
    [0xfd] = ENTRY(CALL, op(ABS16), op(NONE)),
    [0xfe] = ENTRY(CALL, op(REL16), op(NONE)),
    [0xff] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
};

static const RL78Instruction decode_table_2nd_map[256] = {
    /* Row 0 (0x0X) */
    [0x00] = ENTRY(ADD, BREG(X), BREG(A)),
    [0x01] = ENTRY(ADD, BREG(A), BREG(A)),
    [0x02] = ENTRY(ADD, BREG(C), BREG(A)),
    [0x03] = ENTRY(ADD, BREG(B), BREG(A)),
    [0x04] = ENTRY(ADD, BREG(E), BREG(A)),
    [0x05] = ENTRY(ADD, BREG(D), BREG(A)),
    [0x06] = ENTRY(ADD, BREG(L), BREG(A)),
    [0x07] = ENTRY(ADD, BREG(H), BREG(A)),
    [0x08] = ENTRY(ADD, BREG(A), BREG(X)),
    [0x09] = ENTRY(ADDW, WREG(AX), op(IND_REG_IMM)),
    [0x0a] = ENTRY(ADD, BREG(A), BREG(C)),
    [0x0b] = ENTRY(ADD, BREG(A), BREG(B)),
    [0x0c] = ENTRY(ADD, BREG(A), BREG(E)),
    [0x0d] = ENTRY(ADD, BREG(A), BREG(D)),
    [0x0e] = ENTRY(ADD, BREG(A), BREG(L)),
    [0x0f] = ENTRY(ADD, BREG(A), BREG(H)),

    /* Row 1 (0x1X) */
    [0x10] = ENTRY(ADDC, BREG(X), BREG(A)),
    [0x11] = ENTRY(ADDC, BREG(A), BREG(A)),
    [0x12] = ENTRY(ADDC, BREG(C), BREG(A)),
    [0x13] = ENTRY(ADDC, BREG(B), BREG(A)),
    [0x14] = ENTRY(ADDC, BREG(E), BREG(A)),
    [0x15] = ENTRY(ADDC, BREG(D), BREG(A)),
    [0x16] = ENTRY(ADDC, BREG(L), BREG(A)),
    [0x17] = ENTRY(ADDC, BREG(H), BREG(A)),
    [0x18] = ENTRY(ADDC, BREG(A), BREG(X)),
    [0x19] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0x1a] = ENTRY(ADDC, BREG(A), BREG(C)),
    [0x1b] = ENTRY(ADDC, BREG(A), BREG(B)),
    [0x1c] = ENTRY(ADDC, BREG(A), BREG(E)),
    [0x1d] = ENTRY(ADDC, BREG(A), BREG(D)),
    [0x1e] = ENTRY(ADDC, BREG(A), BREG(L)),
    [0x1f] = ENTRY(ADDC, BREG(A), BREG(H)),

    /* Row 2 (0x2X) */
    [0x20] = ENTRY(SUB, BREG(X), BREG(A)),
    [0x21] = ENTRY(SUB, BREG(A), BREG(A)),
    [0x22] = ENTRY(SUB, BREG(C), BREG(A)),
    [0x23] = ENTRY(SUB, BREG(B), BREG(A)),
    [0x24] = ENTRY(SUB, BREG(E), BREG(A)),
    [0x25] = ENTRY(SUB, BREG(D), BREG(A)),
    [0x26] = ENTRY(SUB, BREG(L), BREG(A)),
    [0x27] = ENTRY(SUB, BREG(H), BREG(A)),
    [0x28] = ENTRY(SUB, BREG(A), BREG(X)),
    [0x29] = ENTRY(SUBW, WREG(AX), op(IND_REG_IMM)),
    [0x2a] = ENTRY(SUB, BREG(A), BREG(C)),
    [0x2b] = ENTRY(SUB, BREG(A), BREG(B)),
    [0x2c] = ENTRY(SUB, BREG(A), BREG(E)),
    [0x2d] = ENTRY(SUB, BREG(A), BREG(D)),
    [0x2e] = ENTRY(SUB, BREG(A), BREG(L)),
    [0x2f] = ENTRY(SUB, BREG(A), BREG(H)),

    /* Row 3 (0x3X) */
    [0x30] = ENTRY(SUBC, BREG(X), BREG(A)),
    [0x31] = ENTRY(SUBC, BREG(A), BREG(A)),
    [0x32] = ENTRY(SUBC, BREG(C), BREG(A)),
    [0x33] = ENTRY(SUBC, BREG(B), BREG(A)),
    [0x34] = ENTRY(SUBC, BREG(E), BREG(A)),
    [0x35] = ENTRY(SUBC, BREG(D), BREG(A)),
    [0x36] = ENTRY(SUBC, BREG(L), BREG(A)),
    [0x37] = ENTRY(SUBC, BREG(H), BREG(A)),
    [0x38] = ENTRY(SUBC, BREG(A), BREG(X)),
    [0x39] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0x3a] = ENTRY(SUBC, BREG(A), BREG(C)),
    [0x3b] = ENTRY(SUBC, BREG(A), BREG(B)),
    [0x3c] = ENTRY(SUBC, BREG(A), BREG(E)),
    [0x3d] = ENTRY(SUBC, BREG(A), BREG(D)),
    [0x3e] = ENTRY(SUBC, BREG(A), BREG(L)),
    [0x3f] = ENTRY(SUBC, BREG(A), BREG(H)),

    /* Row 4 (0x4X) */
    [0x40] = ENTRY(CMP, BREG(X), BREG(A)),
    [0x41] = ENTRY(CMP, BREG(A), BREG(A)),
    [0x42] = ENTRY(CMP, BREG(C), BREG(A)),
    [0x43] = ENTRY(CMP, BREG(B), BREG(A)),
    [0x44] = ENTRY(CMP, BREG(E), BREG(A)),
    [0x45] = ENTRY(CMP, BREG(D), BREG(A)),
    [0x46] = ENTRY(CMP, BREG(L), BREG(A)),
    [0x47] = ENTRY(CMP, BREG(H), BREG(A)),
    [0x48] = ENTRY(CMP, BREG(A), BREG(X)),
    [0x49] = ENTRY(CMPW, WREG(AX), op(IND_REG_IMM)),
    [0x4a] = ENTRY(CMP, BREG(A), BREG(C)),
    [0x4b] = ENTRY(CMP, BREG(A), BREG(B)),
    [0x4c] = ENTRY(CMP, BREG(A), BREG(E)),
    [0x4d] = ENTRY(CMP, BREG(A), BREG(D)),
    [0x4e] = ENTRY(CMP, BREG(A), BREG(L)),
    [0x4f] = ENTRY(CMP, BREG(A), BREG(H)),

    /* Row 5 (0x5X) */
    [0x50] = ENTRY(AND, BREG(X), BREG(A)),
    [0x51] = ENTRY(AND, BREG(A), BREG(A)),
    [0x52] = ENTRY(AND, BREG(C), BREG(A)),
    [0x53] = ENTRY(AND, BREG(B), BREG(A)),
    [0x54] = ENTRY(AND, BREG(E), BREG(A)),
    [0x55] = ENTRY(AND, BREG(D), BREG(A)),
    [0x56] = ENTRY(AND, BREG(L), BREG(A)),
    [0x57] = ENTRY(AND, BREG(H), BREG(A)),
    [0x58] = ENTRY(AND, BREG(A), BREG(X)),
    [0x59] = ENTRY(INC, op(IND_REG_IMM), op(NONE)),
    [0x5a] = ENTRY(AND, BREG(A), BREG(C)),
    [0x5b] = ENTRY(AND, BREG(A), BREG(B)),
    [0x5c] = ENTRY(AND, BREG(A), BREG(E)),
    [0x5d] = ENTRY(AND, BREG(A), BREG(D)),
    [0x5e] = ENTRY(AND, BREG(A), BREG(L)),
    [0x5f] = ENTRY(AND, BREG(A), BREG(H)),

    /* Row 6 (0x6X) */
    [0x60] = ENTRY(OR, BREG(X), BREG(A)),
    [0x61] = ENTRY(OR, BREG(A), BREG(A)),
    [0x62] = ENTRY(OR, BREG(C), BREG(A)),
    [0x63] = ENTRY(OR, BREG(B), BREG(A)),
    [0x64] = ENTRY(OR, BREG(E), BREG(A)),
    [0x65] = ENTRY(OR, BREG(D), BREG(A)),
    [0x66] = ENTRY(OR, BREG(L), BREG(A)),
    [0x67] = ENTRY(OR, BREG(H), BREG(A)),
    [0x68] = ENTRY(OR, BREG(A), BREG(X)),
    [0x69] = ENTRY(DEC, op(IND_REG_IMM), op(NONE)),
    [0x6a] = ENTRY(OR, BREG(A), BREG(C)),
    [0x6b] = ENTRY(OR, BREG(A), BREG(B)),
    [0x6c] = ENTRY(OR, BREG(A), BREG(E)),
    [0x6d] = ENTRY(OR, BREG(A), BREG(D)),
    [0x6e] = ENTRY(OR, BREG(A), BREG(L)),
    [0x6f] = ENTRY(OR, BREG(A), BREG(H)),

    /* Row 7 (0x7X) */
    [0x70] = ENTRY(XOR, BREG(X), BREG(A)),
    [0x71] = ENTRY(XOR, BREG(A), BREG(A)),
    [0x72] = ENTRY(XOR, BREG(C), BREG(A)),
    [0x73] = ENTRY(XOR, BREG(B), BREG(A)),
    [0x74] = ENTRY(XOR, BREG(E), BREG(A)),
    [0x75] = ENTRY(XOR, BREG(D), BREG(A)),
    [0x76] = ENTRY(XOR, BREG(L), BREG(A)),
    [0x77] = ENTRY(XOR, BREG(H), BREG(A)),
    [0x78] = ENTRY(XOR, BREG(A), BREG(X)),
    [0x79] = ENTRY(INCW, op(IND_REG_IMM), op(NONE)),
    [0x7a] = ENTRY(XOR, BREG(A), BREG(C)),
    [0x7b] = ENTRY(XOR, BREG(A), BREG(B)),
    [0x7c] = ENTRY(XOR, BREG(A), BREG(E)),
    [0x7d] = ENTRY(XOR, BREG(A), BREG(D)),
    [0x7e] = ENTRY(XOR, BREG(A), BREG(L)),
    [0x7f] = ENTRY(XOR, BREG(A), BREG(H)),

    /* Row 8 (0x8X) */
    [0x80] = ENTRY(ADD, BREG(A), IND_RR(HL, B)),
    [0x81] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0x82] = ENTRY(ADD, BREG(A), IND_RR(HL, C)),
    [0x83] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0x84] = ENTRY(CALLT, CALLT_IDX(0x80), op(NONE)),
    [0x85] = ENTRY(CALLT, CALLT_IDX(0x90), op(NONE)),
    [0x86] = ENTRY(CALLT, CALLT_IDX(0xa0), op(NONE)),
    [0x87] = ENTRY(CALLT, CALLT_IDX(0xb0), op(NONE)),
    [0x88] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0x89] = ENTRY(DECW, op(IND_REG_IMM), op(NONE)),
    [0x8a] = ENTRY(XCH, BREG(A), BREG(C)),
    [0x8b] = ENTRY(XCH, BREG(A), BREG(B)),
    [0x8c] = ENTRY(XCH, BREG(A), BREG(E)),
    [0x8d] = ENTRY(XCH, BREG(A), BREG(D)),
    [0x8e] = ENTRY(XCH, BREG(A), BREG(L)),
    [0x8f] = ENTRY(XCH, BREG(A), BREG(H)),

    /* Row 9 (0x9X) */
    [0x90] = ENTRY(ADDC, BREG(A), IND_RR(HL, B)),
    [0x91] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0x92] = ENTRY(ADDC, BREG(A), IND_RR(HL, C)),
    [0x93] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0x94] = ENTRY(CALLT, CALLT_IDX(0x82), op(NONE)),
    [0x95] = ENTRY(CALLT, CALLT_IDX(0x92), op(NONE)),
    [0x96] = ENTRY(CALLT, CALLT_IDX(0xa2), op(NONE)),
    [0x97] = ENTRY(CALLT, CALLT_IDX(0xb2), op(NONE)),
    [0x98] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0x99] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0x9a] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0x9b] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0x9c] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0x9d] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0x9e] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0x9f] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */

    /* Row A (0xAX) */
    [0xa0] = ENTRY(SUB, BREG(A), IND_RR(HL, B)),
    [0xa1] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0xa2] = ENTRY(SUB, BREG(A), IND_RR(HL, C)),
    [0xa3] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0xa4] = ENTRY(CALLT, CALLT_IDX(0x84), op(NONE)),
    [0xa5] = ENTRY(CALLT, CALLT_IDX(0x94), op(NONE)),
    [0xa6] = ENTRY(CALLT, CALLT_IDX(0xa4), op(NONE)),
    [0xa7] = ENTRY(CALLT, CALLT_IDX(0xb4), op(NONE)),
    [0xa8] = ENTRY(XCH, BREG(A), op(SADDR)),
    [0xa9] = ENTRY(XCH, BREG(A), IND_RR(HL, C)),
    [0xaa] = ENTRY(XCH, BREG(A), op(ABS16)),
    [0xab] = ENTRY(XCH, BREG(A), op(SFR)),
    [0xac] = ENTRY(XCH, BREG(A), IND_RI(HL)),
    [0xad] = ENTRY(XCH, BREG(A), IND_RI(HL)),
    [0xae] = ENTRY(XCH, BREG(A), IND_RI(DE)),
    [0xaf] = ENTRY(XCH, BREG(A), IND_RI(DE)),

    /* Row B (0xBX) */
    [0xb0] = ENTRY(SUBC, BREG(A), IND_RR(HL, B)),
    [0xb1] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0xb2] = ENTRY(SUBC, BREG(A), IND_RR(HL, C)),
    [0xb3] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0xb4] = ENTRY(CALLT, CALLT_IDX(0x86), op(NONE)),
    [0xb5] = ENTRY(CALLT, CALLT_IDX(0x96), op(NONE)),
    [0xb6] = ENTRY(CALLT, CALLT_IDX(0xa6), op(NONE)),
    [0xb7] = ENTRY(CALLT, CALLT_IDX(0xb6), op(NONE)),
    [0xb8] = ENTRY(MOV, op(ES), op(SADDR)),
    [0xb9] = ENTRY(XCH, BREG(A), IND_RR(HL, B)),
    [0xba] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0xbb] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0xbc] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0xbd] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0xbe] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0xbf] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */

    /* Row C (0xCX) */
    [0xc0] = ENTRY(CMP, BREG(A), IND_RR(HL, B)),
    [0xc1] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0xc2] = ENTRY(CMP, BREG(A), IND_RR(HL, C)),
    [0xc3] = ENTRY(BH, op(REL8), op(NONE)),
    [0xc4] = ENTRY(CALLT, CALLT_IDX(0x88), op(NONE)),
    [0xc5] = ENTRY(CALLT, CALLT_IDX(0x98), op(NONE)),
    [0xc6] = ENTRY(CALLT, CALLT_IDX(0xa8), op(NONE)),
    [0xc7] = ENTRY(CALLT, CALLT_IDX(0xb8), op(NONE)),
    [0xc8] = ENTRY(SKC, op(NONE), op(NONE)),
    [0xc9] = ENTRY(MOV, BREG(A), IND_RR(HL, B)),
    [0xca] = ENTRY(CALL, WREG(AX), op(NONE)),
    [0xcb] = ENTRY(BR, WREG(AX), op(NONE)),
    [0xcc] = ENTRY(BRK, op(NONE), op(NONE)),
    [0xcd] = ENTRY(POP, op(PSW), op(NONE)),
    [0xce] = ENTRY(MOVS, BREG(X), op(IND_REG_IMM)),
    [0xcf] = ENTRY(SEL, SEL_RB(0), op(NONE)),

    /* Row D (0xDX) */
    [0xd0] = ENTRY(AND, BREG(A), IND_RR(HL, B)),
    [0xd1] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0xd2] = ENTRY(AND, BREG(A), IND_RR(HL, C)),
    [0xd3] = ENTRY(BNH, op(REL8), op(NONE)),
    [0xd4] = ENTRY(CALLT, CALLT_IDX(0x8a), op(NONE)),
    [0xd5] = ENTRY(CALLT, CALLT_IDX(0x9a), op(NONE)),
    [0xd6] = ENTRY(CALLT, CALLT_IDX(0xaa), op(NONE)),
    [0xd7] = ENTRY(CALLT, CALLT_IDX(0xba), op(NONE)),
    [0xd8] = ENTRY(SKNC, op(NONE), op(NONE)),
    [0xd9] = ENTRY(MOV, IND_RR(HL, B), BREG(A)),
    [0xda] = ENTRY(CALL, WREG(BC), op(NONE)),
    [0xdb] = ENTRY(ROR, BREG(A), op(NONE)),
    [0xdc] = ENTRY(ROLC, BREG(A), op(NONE)),
    [0xdd] = ENTRY(PUSH, op(PSW), op(NONE)),
    [0xde] = ENTRY(CMPS, BREG(X), op(IND_REG_IMM)),
    [0xdf] = ENTRY(SEL, SEL_RB(1), op(NONE)),

    /* Row E (0xEX) */
    [0xe0] = ENTRY(OR, BREG(A), IND_RR(HL, B)),
    [0xe1] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0xe2] = ENTRY(OR, BREG(A), IND_RR(HL, C)),
    [0xe3] = ENTRY(SKH, op(NONE), op(NONE)),
    [0xe4] = ENTRY(CALLT, CALLT_IDX(0x8c), op(NONE)),
    [0xe5] = ENTRY(CALLT, CALLT_IDX(0x9c), op(NONE)),
    [0xe6] = ENTRY(CALLT, CALLT_IDX(0xac), op(NONE)),
    [0xe7] = ENTRY(CALLT, CALLT_IDX(0xbc), op(NONE)),
    [0xe8] = ENTRY(SKZ, op(NONE), op(NONE)),
    [0xe9] = ENTRY(MOV, BREG(A), IND_RR(HL, C)),
    [0xea] = ENTRY(CALL, WREG(DE), op(NONE)),
    [0xeb] = ENTRY(ROL, BREG(A), op(NONE)),
    [0xec] = ENTRY(RETB, op(NONE), op(NONE)),
    [0xed] = ENTRY(HALT, op(NONE), op(NONE)),
    [0xee] = ENTRY(ROLWC, WREG(AX), op(NONE)),
    [0xef] = ENTRY(SEL, SEL_RB(2), op(NONE)),

    /* Row F (0xFX) */
    [0xf0] = ENTRY(XOR, BREG(A), IND_RR(HL, B)),
    [0xf1] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* 空き */
    [0xf2] = ENTRY(XOR, BREG(A), IND_RR(HL, C)),
    [0xf3] = ENTRY(SKNH, op(NONE), op(NONE)),
    [0xf4] = ENTRY(CALLT, CALLT_IDX(0x8e), op(NONE)),
    [0xf5] = ENTRY(CALLT, CALLT_IDX(0x9e), op(NONE)),
    [0xf6] = ENTRY(CALLT, CALLT_IDX(0xae), op(NONE)),
    [0xf7] = ENTRY(CALLT, CALLT_IDX(0xbe), op(NONE)),
    [0xf8] = ENTRY(SKNZ, op(NONE), op(NONE)),
    [0xf9] = ENTRY(MOV, IND_RR(HL, C), BREG(A)),
    [0xfa] = ENTRY(CALL, WREG(HL), op(NONE)),
    [0xfb] = ENTRY(RORC, BREG(A), op(NONE)),
    [0xfc] = ENTRY(RETI, op(NONE), op(NONE)),
    [0xfd] = ENTRY(STOP, op(NONE), op(NONE)),
    [0xfe] = ENTRY(ROLWC, WREG(BC), op(NONE)),
    [0xff] = ENTRY(SEL, SEL_RB(3), op(NONE)),
};

#define MAP_3RD_00_7F(n)                                                       \
    [0x00 + (n) * 0x10] = ENTRY(SET1, BIT_OP(ABS16, n), op(NONE)),   \
    [0x01 + (n) * 0x10] = ENTRY(MOV1, BIT_OP(SADDR, n), op(CY)),     \
    [0x02 + (n) * 0x10] = ENTRY(SET1, BIT_OP(SADDR, n), op(NONE)),   \
    [0x03 + (n) * 0x10] = ENTRY(CLR1, BIT_OP(SADDR, n), op(NONE)),   \
    [0x04 + (n) * 0x10] = ENTRY(MOV1, op(CY), BIT_OP(SADDR, n)),     \
    [0x05 + (n) * 0x10] = ENTRY(AND1, op(CY), BIT_OP(SADDR, n)),     \
    [0x06 + (n) * 0x10] = ENTRY(OR1, op(CY), BIT_OP(SADDR, n)),      \
    [0x07 + (n) * 0x10] = ENTRY(XOR1, op(CY), BIT_OP(SADDR, n)),     \
    [0x08 + (n) * 0x10] = ENTRY(CLR1, BIT_OP(ABS16, n), op(NONE)),   \
    [0x09 + (n) * 0x10] = ENTRY(MOV1, BIT_OP(SFR, n), op(CY)),       \
    [0x0a + (n) * 0x10] = ENTRY(SET1, BIT_OP(SFR, n), op(NONE)),     \
    [0x0b + (n) * 0x10] = ENTRY(CLR1, BIT_OP(SFR, n), op(NONE)),     \
    [0x0c + (n) * 0x10] = ENTRY(MOV1, op(CY), BIT_OP(SFR, n)),       \
    [0x0d + (n) * 0x10] = ENTRY(AND1, op(CY), BIT_OP(SFR, n)),       \
    [0x0e + (n) * 0x10] = ENTRY(OR1, op(CY), BIT_OP(SFR, n)),        \
    [0x0f + (n) * 0x10] = ENTRY(XOR1, op(CY), BIT_OP(SFR, n))

#define MAP_3RD_80_FF(n)                                                       \
    [0x81 + (n) * 0x10] = ENTRY(MOV1, BIT_OP(IND_HL, n), op(CY)),    \
    [0x82 + (n) * 0x10] = ENTRY(SET1, BIT_OP(IND_HL, n), op(NONE)),  \
    [0x83 + (n) * 0x10] = ENTRY(CLR1, BIT_OP(IND_HL, n), op(NONE)),  \
    [0x84 + (n) * 0x10] = ENTRY(MOV1, op(CY), BIT_OP(IND_HL, n)),    \
    [0x85 + (n) * 0x10] = ENTRY(AND1, op(CY), BIT_OP(IND_HL, n)),    \
    [0x86 + (n) * 0x10] = ENTRY(OR1, op(CY), BIT_OP(IND_HL, n)),     \
    [0x87 + (n) * 0x10] = ENTRY(XOR1, op(CY), BIT_OP(IND_HL, n)),    \
    [0x89 + (n) * 0x10] = ENTRY(MOV1, BIT_OP(REG_A, n), op(CY)),     \
    [0x8a + (n) * 0x10] = ENTRY(SET1, BIT_OP(REG_A, n), op(NONE)),   \
    [0x8b + (n) * 0x10] = ENTRY(CLR1, BIT_OP(REG_A, n), op(NONE)),   \
    [0x8c + (n) * 0x10] = ENTRY(MOV1, op(CY), BIT_OP(REG_A, n)),     \
    [0x8d + (n) * 0x10] = ENTRY(AND1, op(CY), BIT_OP(REG_A, n)),     \
    [0x8e + (n) * 0x10] = ENTRY(OR1, op(CY), BIT_OP(REG_A, n)),      \
    [0x8f + (n) * 0x10] = ENTRY(XOR1, op(CY), BIT_OP(REG_A, n))

static const RL78Instruction decode_table_3rd_map[256] = {
    MAP_3RD_00_7F(0),
    MAP_3RD_00_7F(1),
    MAP_3RD_00_7F(2),
    MAP_3RD_00_7F(3),
    MAP_3RD_00_7F(4),
    MAP_3RD_00_7F(5),
    MAP_3RD_00_7F(6),
    MAP_3RD_00_7F(7),

    MAP_3RD_80_FF(0),
    MAP_3RD_80_FF(1),
    MAP_3RD_80_FF(2),
    MAP_3RD_80_FF(3),
    MAP_3RD_80_FF(4),
    MAP_3RD_80_FF(5),
    MAP_3RD_80_FF(6),
    MAP_3RD_80_FF(7),

    [0x80] = ENTRY(SET1, op(CY), op(NONE)),
    [0x88] = ENTRY(CLR1, op(CY), op(NONE)),
    [0x90] = ENTRY(UNKNOWN, op(NONE), op(NONE)),
    [0x98] = ENTRY(UNKNOWN, op(NONE), op(NONE)),
    [0xa0] = ENTRY(UNKNOWN, op(NONE), op(NONE)),
    [0xa8] = ENTRY(UNKNOWN, op(NONE), op(NONE)),
    [0xb0] = ENTRY(UNKNOWN, op(NONE), op(NONE)),
    [0xb8] = ENTRY(UNKNOWN, op(NONE), op(NONE)),
    [0xc0] = ENTRY(NOT1, op(CY), op(NONE)),
    [0xc8] = ENTRY(UNKNOWN, op(NONE), op(NONE)),
    [0xd0] = ENTRY(UNKNOWN, op(NONE), op(NONE)),
    [0xd8] = ENTRY(UNKNOWN, op(NONE), op(NONE)),
    [0xe0] = ENTRY(UNKNOWN, op(NONE), op(NONE)),
    [0xe8] = ENTRY(UNKNOWN, op(NONE), op(NONE)),
    [0xf0] = ENTRY(UNKNOWN, op(NONE), op(NONE)),
    [0xf8] = ENTRY(UNKNOWN, op(NONE), op(NONE)),
};

#define MAP_4TH_00_75(n)                                            \
    [0x00 + (n) * 0x10] = ENTRY(BTCLR, BIT_OP(SADDR, n), op(REL8)), \
    [0x01 + (n) * 0x10] = ENTRY(BTCLR, BIT_OP(REG_A, n), op(REL8)), \
    [0x02 + (n) * 0x10] = ENTRY(BT, BIT_OP(SADDR, n), op(REL8)),    \
    [0x03 + (n) * 0x10] = ENTRY(BT, BIT_OP(REG_A, n), op(REL8)),    \
    [0x04 + (n) * 0x10] = ENTRY(BF, BIT_OP(SADDR, n), op(REL8)),    \
    [0x05 + (n) * 0x10] = ENTRY(BF, BIT_OP(REG_A, n), op(REL8))

#define MAP_4TH_17_7B(n)                                                       \
    [0x07 + (n) * 0x10] = ENTRY(SHL, BREG(C), SHAMT(n)), \
    [0x08 + (n) * 0x10] = ENTRY(SHL, BREG(B), SHAMT(n)), \
    [0x09 + (n) * 0x10] = ENTRY(SHL, BREG(A), SHAMT(n)), \
    [0x0a + (n) * 0x10] = ENTRY(SHR, BREG(A), SHAMT(n)), \
    [0x0b + (n) * 0x10] = ENTRY(SAR, BREG(A), SHAMT(n))

#define MAP_4TH_1C_FF(n)                                                       \
    [0x0c + (n) * 0x10] = ENTRY(SHLW, WREG(BC), SHAMT(n)), \
    [0x0d + (n) * 0x10] = ENTRY(SHLW, WREG(AX), SHAMT(n)), \
    [0x0e + (n) * 0x10] = ENTRY(SHRW, WREG(AX), SHAMT(n)), \
    [0x0f + (n) * 0x10] = ENTRY(SARW, WREG(AX), SHAMT(n))

#define MAP_4TH_80_F5(n)                                                       \
    [0x80 + (n) * 0x10] = ENTRY(BTCLR, BIT_OP(SFR, n), op(REL8)),    \
    [0x81 + (n) * 0x10] = ENTRY(BTCLR, BIT_OP(IND_HL, n), op(REL8)), \
    [0x82 + (n) * 0x10] = ENTRY(BT, BIT_OP(SFR, n), op(REL8)),       \
    [0x83 + (n) * 0x10] = ENTRY(BT, BIT_OP(IND_HL, n), op(REL8)),    \
    [0x84 + (n) * 0x10] = ENTRY(BF, BIT_OP(SFR, n), op(REL8)),       \
    [0x85 + (n) * 0x10] = ENTRY(BF, BIT_OP(IND_HL, n), op(REL8))

static const RL78Instruction decode_table_4th_map[256] = {
    MAP_4TH_00_75(0),
    MAP_4TH_00_75(1),
    MAP_4TH_00_75(2),
    MAP_4TH_00_75(3),
    MAP_4TH_00_75(4),
    MAP_4TH_00_75(5),

    MAP_4TH_17_7B(1),
    MAP_4TH_17_7B(2),
    MAP_4TH_17_7B(3),
    MAP_4TH_17_7B(4),
    MAP_4TH_17_7B(5),

    MAP_4TH_1C_FF(1),
    MAP_4TH_1C_FF(2),
    MAP_4TH_1C_FF(3),
    MAP_4TH_1C_FF(4),
    MAP_4TH_1C_FF(5),
    MAP_4TH_1C_FF(6),
    MAP_4TH_1C_FF(7),
    MAP_4TH_1C_FF(8),
    MAP_4TH_1C_FF(9),
    MAP_4TH_1C_FF(10),
    MAP_4TH_1C_FF(11),
    MAP_4TH_1C_FF(12),
    MAP_4TH_1C_FF(13),
    MAP_4TH_1C_FF(14),
    MAP_4TH_1C_FF(15),

    MAP_4TH_80_F5(0),
    MAP_4TH_80_F5(1),
    MAP_4TH_80_F5(2),
    MAP_4TH_80_F5(3),
    MAP_4TH_80_F5(4),
    MAP_4TH_80_F5(5),

    [0x06] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x07] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x08] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x09] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x0a] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x0b] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x0c] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x0d] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x0e] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x0f] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */

    [0x16] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x26] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x36] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x46] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x56] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x66] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x76] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */

    [0x86] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x87] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x88] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x89] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x8a] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x8b] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */

    [0x96] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x97] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x98] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x99] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x9a] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0x9b] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */

    [0xa6] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xa7] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xa8] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xa9] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xaa] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xab] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */

    [0xb6] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xb7] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xb8] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xb9] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xba] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xbb] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */

    [0xc6] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xc7] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xc8] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xc9] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xca] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xcb] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */

    [0xd6] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xd7] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xd8] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xd9] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xda] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xdb] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */

    [0xe6] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xe7] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xe8] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xe9] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xea] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xeb] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */

    [0xf6] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xf7] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xf8] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xf9] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xfa] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
    [0xfb] = ENTRY(UNKNOWN, op(NONE), op(NONE)), /* Empty */
};

enum UnknownHandlerResult {
    UNKNOWN_HANDLER_UNKNOWN_INSN,
    UNKNOWN_HANDLER_USE_MAP2,
    UNKNOWN_HANDLER_USE_MAP3,
    UNKNOWN_HANDLER_USE_MAP4,
    UNKNOWN_HANDLER_USE_PREFIX,
};
typedef enum UnknownHandlerResult UnknownHandlerResult;

static RL78Operand decode_dummy_op(DisasContext *ctx,
                                   const DecodeHandler *handler,
                                   const RL78Operand op)
{
    return op;
}

static RL78Operand decode_byte_op(DisasContext *ctx,
                                  const DecodeHandler *handler,
                                  const RL78Operand op)
{
    const uint32_t pc   = handler->get_pc(ctx);
    const uint8_t imm   = handler->load_byte(ctx, pc);
    RL78Operand operand = op;

    handler->set_pc(ctx, pc + 1);
    operand.const_op = imm;

    return operand;
}

static RL78Operand decode_word_op(DisasContext *ctx,
                                  const DecodeHandler *handler,
                                  const RL78Operand op)
{
    const uint32_t pc   = handler->get_pc(ctx);
    const uint32_t imml = handler->load_byte(ctx, pc + 0);
    const uint32_t immh = handler->load_byte(ctx, pc + 1);
    const uint32_t imm  = (immh << 8) | imml;
    RL78Operand operand = op;

    handler->set_pc(ctx, pc + 2);
    operand.const_op = imm;

    return operand;
}

static RL78Operand decode_20bit_op(DisasContext *ctx,
                                   const DecodeHandler *handler,
                                   const RL78Operand op)
{
    const uint32_t pc    = handler->get_pc(ctx);
    const uint32_t datal = handler->load_byte(ctx, pc + 0);
    const uint32_t datah = handler->load_byte(ctx, pc + 1);
    const uint32_t datas = handler->load_byte(ctx, pc + 2);
    const uint32_t data  = (datas << 16) | (datah << 8) | datal;
    RL78Operand operand  = op;

    handler->set_pc(ctx, pc + 3);
    operand.const_op = data;

    return operand;
}

static RL78Operand decode_ind_reg_imm(DisasContext *ctx,
                                      const DecodeHandler *handler,
                                      const RL78Operand op)
{
    const uint32_t pc   = handler->get_pc(ctx);
    const uint8_t imm   = handler->load_byte(ctx, pc);
    RL78Operand operand = op;

    handler->set_pc(ctx, pc + 1);
    operand.ind_reg_imm.imm = imm;

    return operand;
}

static RL78Operand decode_ind_sp_imm(DisasContext *ctx,
                                     const DecodeHandler *handler,
                                     const RL78Operand op)
{
    const uint32_t pc   = handler->get_pc(ctx);
    const uint8_t imm   = handler->load_byte(ctx, pc);
    RL78Operand operand = op;

    handler->set_pc(ctx, pc + 1);
    operand.ind_sp_imm.imm = imm;

    return operand;
}

static RL78Operand decode_ind_base_byte(DisasContext *ctx,
                                        const DecodeHandler *handler,
                                        const RL78Operand op)
{
    const uint32_t pc    = handler->get_pc(ctx);
    const uint32_t basel = handler->load_byte(ctx, pc + 0);
    const uint32_t baseh = handler->load_byte(ctx, pc + 1) << 8;
    const uint32_t base  = (baseh << 8) | basel;
    RL78Operand operand  = op;

    handler->set_pc(ctx, pc + 2);
    operand.ind_base_byte.base = base;

    return operand;
}

static RL78Operand decode_ind_base_word(DisasContext *ctx,
                                        const DecodeHandler *handler,
                                        const RL78Operand op)
{
    const uint32_t pc    = handler->get_pc(ctx);
    const uint32_t basel = handler->load_byte(ctx, pc + 0);
    const uint32_t baseh = handler->load_byte(ctx, pc + 1) << 8;
    const uint32_t base  = (baseh << 8) | basel;
    RL78Operand operand  = op;

    handler->set_pc(ctx, pc + 2);
    operand.ind_base_word.base = base;

    return operand;
}

static RL78Operand decode_bit_op(DisasContext *ctx,
                                 const DecodeHandler *handler,
                                 const RL78Operand op)
{
    const uint32_t pc   = handler->get_pc(ctx);
    RL78Operand operand = op;

    switch (op.kind) {
    case RL78_OP_SADDR:
    case RL78_OP_SFR: {
        const uint8_t bit_op = handler->load_byte(ctx, pc);

        handler->set_pc(ctx, pc + 1);
        operand.bit.addr = bit_op;

        break;
    }
    default:
        operand.bit.addr = 0;
        break;
    }

    return operand;
}

static RL78Operand (*decode_operand_table[RL78_OP_NUM])(DisasContext *,
                                                        const DecodeHandler *,
                                                        const RL78Operand) = {
    [RL78_OP_NONE]          = decode_dummy_op,
    [RL78_OP_BYTE_REG]      = decode_dummy_op,
    [RL78_OP_WORD_REG]      = decode_dummy_op,
    [RL78_OP_PSW]           = decode_dummy_op,
    [RL78_OP_SP]            = decode_dummy_op,
    [RL78_OP_ES]            = decode_dummy_op,
    [RL78_OP_CY]            = decode_dummy_op,
    [RL78_OP_BIT]           = decode_bit_op,
    [RL78_OP_IMM8]          = decode_byte_op,
    [RL78_OP_IMM16]         = decode_word_op,
    [RL78_OP_ABS16]         = decode_word_op,
    [RL78_OP_ABS20]         = decode_20bit_op,
    [RL78_OP_SADDR]         = decode_byte_op,
    [RL78_OP_SFR]           = decode_byte_op,
    [RL78_OP_IND_REG_REG]   = decode_dummy_op,
    [RL78_OP_IND_REG_IMM]   = decode_ind_reg_imm,
    [RL78_OP_IND_SP_IMM]    = decode_ind_sp_imm,
    [RL78_OP_IND_BASE_BYTE] = decode_ind_base_byte,
    [RL78_OP_IND_BASE_WORD] = decode_ind_base_word,
    [RL78_OP_SHAMT]         = decode_dummy_op,
    [RL78_OP_REL8]          = decode_byte_op,
    [RL78_OP_REL16]         = decode_word_op,
    [RL78_OP_CALLT]         = decode_dummy_op,
    [RL78_OP_SEL_RB]        = decode_dummy_op,
};

static UnknownHandlerResult unknown_handler(const RL78Instruction *map,
                                            const bool is_first_byte,
                                            const uint8_t byte)
{
    if (map == decode_table_1st_map && is_first_byte && byte == 0x11) {
        return UNKNOWN_HANDLER_USE_PREFIX;
    }
    if (map == decode_table_1st_map && byte == 0x31) {
        return UNKNOWN_HANDLER_USE_MAP4;
    }
    if (map == decode_table_1st_map && byte == 0x61) {
        return UNKNOWN_HANDLER_USE_MAP2;
    }
    if (map == decode_table_1st_map && byte == 0x71) {
        return UNKNOWN_HANDLER_USE_MAP3;
    }

    return UNKNOWN_HANDLER_UNKNOWN_INSN;
}

bool decode(DisasContext *ctx, const DecodeHandler *handler)
{
    const RL78Instruction *map = decode_table_1st_map;
    RL78Instruction insn;

    bool is_first_byte = true;

    while (true) {
        const uint32_t pc  = handler->get_pc(ctx);
        const uint8_t byte = handler->load_byte(ctx, pc);

        insn = map[byte];

        if (insn.mnemonic != RL78_INSN_UNKNOWN) {
            handler->set_pc(ctx, pc + 1);
            break;
        }

        switch (unknown_handler(map, byte, is_first_byte)) {
        case UNKNOWN_HANDLER_USE_MAP2:
            map = decode_table_2nd_map;
            break;
        case UNKNOWN_HANDLER_USE_MAP3:
            map = decode_table_3rd_map;
            break;
        case UNKNOWN_HANDLER_USE_MAP4:
            map = decode_table_4th_map;
            break;
        case UNKNOWN_HANDLER_USE_PREFIX:
            handler->set_es(ctx, true);
            break;
        default:
            return false;
        }

        handler->set_pc(ctx, pc + 1);
        is_first_byte = false;
    }

    for (int i = 0; i < ARRAY_SIZE(insn.operand); i++) {
        const RL78OperandKind kind = insn.operand[i].kind;
        const RL78Operand operand  = insn.operand[i];
        insn.operand[i] = decode_operand_table[kind](ctx, handler, operand);
    }

    return handler->translator_table[insn.mnemonic](ctx, &insn);
}
