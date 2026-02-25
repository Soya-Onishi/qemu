#ifndef TARGET_RL78_DECODE_H
#define TARGET_RL78_DECODE_H

#include "cpu.h"

typedef struct DisasContext DisasContext;

enum RL78Mnemonic {
    RL78_INSN_MOV,
    RL78_INSN_XCH,
    RL78_INSN_ONEB,
    RL78_INSN_CLRB,

    RL78_INSN_MOVW,
    RL78_INSN_XCHW,
    RL78_INSN_ONEW,
    RL78_INSN_CLRW,

    RL78_INSN_ADD,
    RL78_INSN_ADDC,
    RL78_INSN_SUB,
    RL78_INSN_SUBC,
    RL78_INSN_AND,
    RL78_INSN_OR,
    RL78_INSN_XOR,
    RL78_INSN_CMP,
    RL78_INSN_CMP0,
    RL78_INSN_CMPS,
    RL78_INSN_MOVS,

    RL78_INSN_ADDW,
    RL78_INSN_SUBW,
    RL78_INSN_CMPW,

    RL78_INSN_MULU,
    /* Those instructions are executed via SFR accesses */
    // RL78_INSN_MULHU,
    // RL78_INSN_MULH,
    // RL78_INSN_DIVHU,
    // RL78_INSN_DIVWU,
    // RL78_INSN_MACHU,
    // RL78_INSN_MACH,

    RL78_INSN_INC,
    RL78_INSN_DEC,
    RL78_INSN_INCW,
    RL78_INSN_DECW,

    RL78_INSN_SHR,
    RL78_INSN_SHRW,
    RL78_INSN_SHL,
    RL78_INSN_SHLW,
    RL78_INSN_SAR,
    RL78_INSN_SARW,

    RL78_INSN_ROR,
    RL78_INSN_ROL,
    RL78_INSN_RORC,
    RL78_INSN_ROLC,
    RL78_INSN_ROLWC,

    RL78_INSN_MOV1,
    RL78_INSN_AND1,
    RL78_INSN_OR1,
    RL78_INSN_XOR1,
    RL78_INSN_SET1,
    RL78_INSN_CLR1,
    RL78_INSN_NOT1,

    RL78_INSN_CALL,
    RL78_INSN_CALLT,
    RL78_INSN_BRK,
    RL78_INSN_HALT,
    RL78_INSN_STOP,
    RL78_INSN_RET,
    RL78_INSN_RETI,
    RL78_INSN_RETB,

    RL78_INSN_PUSH,
    RL78_INSN_POP,

    RL78_INSN_BR,
    RL78_INSN_BC,
    RL78_INSN_BNC,
    RL78_INSN_BZ,
    RL78_INSN_BNZ,
    RL78_INSN_BH,
    RL78_INSN_BNH,
    RL78_INSN_BT,
    RL78_INSN_BF,
    RL78_INSN_BTCLR,

    RL78_INSN_SKC,
    RL78_INSN_SKNC,
    RL78_INSN_SKZ,
    RL78_INSN_SKNZ,
    RL78_INSN_SKH,
    RL78_INSN_SKNH,

    RL78_INSN_SEL,
    RL78_INSN_NOP,

    RL78_INSN_UNKNOWN,
};
typedef enum RL78Mnemonic RL78Mnemonic;

enum RL78OperandKind {
    RL78_OP_NONE,
    RL78_OP_BYTE_REG,
    RL78_OP_WORD_REG,
    RL78_OP_PSW,
    RL78_OP_SP,
    RL78_OP_ES,
    RL78_OP_CY,
    RL78_OP_BIT,
    RL78_OP_IMM8,
    RL78_OP_IMM16,
    RL78_OP_ABS16,
    RL78_OP_ABS20,
    RL78_OP_SADDR,
    RL78_OP_SFR,
    RL78_OP_IND_REG_REG,
    RL78_OP_IND_REG_IMM,
    RL78_OP_IND_SP_IMM,
    RL78_OP_IND_BASE_BYTE,
    RL78_OP_IND_BASE_WORD,
    RL78_OP_SHAMT,
    RL78_OP_REL8,
    RL78_OP_REL16,
    RL78_OP_CALLT,
    RL78_OP_SEL_RB,
    RL78_OP_NUM
};
typedef enum RL78OperandKind RL78OperandKind;

enum RL78BitOpKind {
    RL78_BITOP_SADDR,
    RL78_BITOP_SFR,
    RL78_BITOP_REG_A,
    RL78_BITOP_ABS16,
    RL78_BITOP_IND_HL,
};
typedef enum RL78BitOpKind RL78BitOpKind;

struct RL78OperandIndRegReg {
    RL78WordRegister base;
    RL78ByteRegister idx;
};

struct RL78OperandIndRegImm {
    RL78WordRegister base;
    uint32_t imm;
};

struct RL78OperandIndSPImm {
    uint32_t imm;
};

struct RL78OperandIndBaseByte {
    uint32_t base;
    RL78ByteRegister idx;
};

struct RL78OperandIndBaseWord {
    uint32_t base;
    RL78WordRegister idx;
};

struct RL78OperandBit {
    RL78BitOpKind kind;
    uint32_t bit;
    union {
        uint32_t addr;
    };
};

typedef struct RL78OperandIndRegReg RL78OperandIndRegReg;
typedef struct RL78OperandIndRegImm RL78OperandIndRegImm;
typedef struct RL78OperandIndSPImm RL78OperandIndSPImm;
typedef struct RL78OperandIndBaseByte RL78OperandIndBaseByte;
typedef struct RL78OperandIndBaseWord RL78OperandIndBaseWord;
typedef struct RL78OperandBit RL78OperandBit;

struct RL78Operand {
    RL78OperandKind kind;
    union {
        RL78ByteRegister byte_reg;
        RL78WordRegister word_reg;
        uint32_t const_op;
        RL78OperandIndRegReg ind_reg_reg;
        RL78OperandIndRegImm ind_reg_imm;
        RL78OperandIndSPImm ind_sp_imm;
        RL78OperandIndBaseByte ind_base_byte;
        RL78OperandIndBaseWord ind_base_word;
        RL78OperandBit bit;
    };
};
typedef struct RL78Operand RL78Operand;

struct RL78Instruction {
    RL78Mnemonic mnemonic;
    RL78Operand operand[2];
};
typedef struct RL78Instruction RL78Instruction;

typedef bool (*TranslateHandler)(DisasContext *ctx, RL78Instruction *insn);

struct DecodeHandler {
    uint8_t (*load_byte)(DisasContext *ctx, uint32_t pc);

    void (*set_pc)(DisasContext *ctx, uint32_t pc);
    uint32_t (*get_pc)(DisasContext *ctx);

    void (*set_es)(DisasContext *ctx, bool es);

    TranslateHandler *translator_table;
};
typedef struct DecodeHandler DecodeHandler;

bool decode(DisasContext *ctx, const DecodeHandler *handler);

#endif
