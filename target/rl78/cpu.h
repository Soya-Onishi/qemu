#ifndef RL78_CPU_H
#define RL78_CPU_H

#include "hw/core/registerfields.h"
#include "cpu-qom.h"
#include "exec/target_long.h"

#ifdef CONFIG_USER_ONLY
#error "RL78 does not support user mode emulation"
#endif

#define REGISTER_BANK_NUM (4)

#define RL78_CPU_PROP_MR_SYSTEM "mr-system"
#define RL78_CPU_PROP_MR_CONTROL "mr-control"
#define RL78_CPU_PROP_MR_ALIAS "mr-alias"

#define RL78_CPU_PROP_STANDARD_SFR "standard-sfr"
#define RL78_CPU_PROP_EXTENDED_SFR "extended-sfr"
#define RL78_CPU_PROP_MIRROR "mirror"

/* PSW define */
REG32(PSW, 0)
FIELD(PSW, CY,   0, 1)
FIELD(PSW, ISP,  1, 2)
FIELD(PSW, RBS0, 3, 1)
FIELD(PSW, AC,   4, 1)
FIELD(PSW, RBS1, 5, 1)
FIELD(PSW, Z,    6, 1)
FIELD(PSW, IE,   7, 1)

typedef enum {
    RL78_AS_SYSTEM,
    RL78_AS_CONTROL,
    RL78_AS_ALIAS,
    RL78_AS_NUM,
} RL78AddressSpace;

typedef enum {
    RL78_BYTE_REG_X = 0,
    RL78_BYTE_REG_A,
    RL78_BYTE_REG_C,
    RL78_BYTE_REG_B,
    RL78_BYTE_REG_E,
    RL78_BYTE_REG_D,
    RL78_BYTE_REG_L,
    RL78_BYTE_REG_H,
} RL78ByteRegister;

typedef enum {
    RL78_WORD_REG_AX = 0,
    RL78_WORD_REG_BC,
    RL78_WORD_REG_DE,
    RL78_WORD_REG_HL,
} RL78WordRegister;

typedef struct RL78PSW {
    uint32_t cy;
    uint32_t isp;
    uint32_t rbs;
    uint32_t ac;
    uint32_t z;
    uint32_t ie;
} RL78PSW;

typedef struct CPUArchState {
    RL78PSW psw;

    /* Control Registers */
    uint32_t sp;
    uint32_t pc;
    uint32_t pmc;

    /* Segment Registers */
    uint32_t es;
    uint32_t cs;

    /* Skip Instruction Control */
    uint32_t skip_en;
    uint32_t skip_req;
} CPURL78State;

struct ArchCPU {
    CPUState parent_obj;

    CPURL78State env;

    MemoryRegion *system;
    MemoryRegion *control;
    MemoryRegion *alias;

    MemMapEntry standard_sfr;
    MemMapEntry extended_sfr;
    MemMapEntry mirror;
};

struct RL78CPUClass {
    CPUClass parent_class;

    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

#define CPU_RESOLVING_TYPE TYPE_RL78_CPU

void rl78_cpu_do_interrupt(CPUState *cs);
bool rl78_cpu_exec_interrupt(CPUState *cs, int interrupt_request);
hwaddr rl78_cpu_get_phys_page_debug(CPUState *cs, vaddr addr);

void rl78_translate_init(void);
void rl78_translate_code(CPUState *cs, TranslationBlock *tb, 
                         int *max_insns, vaddr pc, void *host_pc);

void rl78_cpu_dump_state(CPUState *cs, FILE *f, int flags);

const char* rl78_cpu_gdb_arch_name(CPUState *cs);
int rl78_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n);
int rl78_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n);

static inline uint8_t rl78_cpu_pack_psw(RL78PSW psw_reg) 
{
    uint8_t psw = 0;

    psw = FIELD_DP32(psw, PSW, CY,   psw_reg.cy);
    psw = FIELD_DP32(psw, PSW, ISP,  psw_reg.isp);
    psw = FIELD_DP32(psw, PSW, RBS0, (psw_reg.rbs >> 0) & 0x1);
    psw = FIELD_DP32(psw, PSW, AC,   psw_reg.ac);
    psw = FIELD_DP32(psw, PSW, RBS1, (psw_reg.rbs >> 1) & 0x1);
    psw = FIELD_DP32(psw, PSW, Z,    psw_reg.z);
    psw = FIELD_DP32(psw, PSW, IE,   psw_reg.ie);

    return psw;
}

static inline RL78PSW rl78_cpu_unpack_psw(uint8_t psw) 
{
    RL78PSW psw_reg;

    psw_reg.cy = FIELD_EX32(psw, PSW, CY);
    psw_reg.isp = FIELD_EX32(psw, PSW, ISP);
    psw_reg.rbs = FIELD_EX32(psw, PSW, RBS0) | (FIELD_EX32(psw, PSW, RBS1) << 1);
    psw_reg.ac = FIELD_EX32(psw, PSW, AC);
    psw_reg.z = FIELD_EX32(psw, PSW, Z);
    psw_reg.ie = FIELD_EX32(psw, PSW, IE);

    return psw_reg;
}

#endif
