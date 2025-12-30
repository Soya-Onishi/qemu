#ifndef RL78_CPU_H
#define RL78_CPU_H

#include "hw/registerfields.h"
#include "cpu-qom.h"

#include "exec/cpu-common.h"
#include "exec/cpu-defs.h"
#include "exec/cpu-interrupt.h"

#ifdef CONFIG_USER_ONLY
#error "RL78 does not support user mode emulation"
#endif

/* PSW define */
REG32(PSW, 0)
FIELD(PSW, CY,   0, 1)
FIELD(PSW, ISP,  1, 2)
FIELD(PSW, RBS0, 3, 1)
FIELD(PSW, AC,   4, 1)
FIELD(PSW, RBS1, 5, 1)
FIELD(PSW, Z,    6, 1)
FIELD(PSW, IE,   7, 1)

typedef enum RL78GPRegister {
    RL78_GPREG_X,
    RL78_GPREG_A,
    RL78_GPREG_C,
    RL78_GPREG_B,
    RL78_GPREG_E,
    RL78_GPREG_D,
    RL78_GPREG_L,
    RL78_GPREG_H,
    GPREG_NUM
} RL78GPRegister;

#define GPREG_BANK_NUM (4)

typedef struct RL78PSWReg {
    uint32_t cy;
    uint32_t isp;
    uint32_t rbs;
    uint32_t ac;
    uint32_t z;
    uint32_t ie;
} RL78PSWReg;

typedef struct CPUArchState {
    /* general purpose register */
    uint32_t regs[GPREG_BANK_NUM][GPREG_NUM]; 

    /* system registers */
    RL78PSWReg psw; /* status register          */
    uint32_t sp;    /* stack pointer            */
    uint32_t pc;    /* program counter          */
    uint32_t pmc;   /* processor mode control   */

    /* registers for upper part of 20bit addresses */
    uint32_t es;
    uint32_t cs;

    /* interrupt info */
    uint8_t req_irq;
    uint8_t req_isp;
    uint8_t ack_irq;
    uint8_t ack_isp;

    /* internal use */
    uint32_t skip;
} CPURL78State;

/**
 * RL78CPU:
 * @env: #CPURL78State
 * 
 * A RL78 CPU
 */
struct ArchCPU {
    CPUState parent_obj;

    CPURL78State env;
};

/**
 * RL78CPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_phases: The parent class's reset phase handlers.
 */
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

static inline uint8_t rl78_cpu_pack_psw(RL78PSWReg psw_reg) 
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

static inline RL78PSWReg rl78_cpu_unpack_psw(uint8_t psw) 
{
    RL78PSWReg psw_reg;

    psw_reg.cy = FIELD_EX32(psw, PSW, CY);
    psw_reg.isp = FIELD_EX32(psw, PSW, ISP);
    psw_reg.rbs = FIELD_EX32(psw, PSW, RBS0) | (FIELD_EX32(psw, PSW, RBS1) << 1);
    psw_reg.ac = FIELD_EX32(psw, PSW, AC);
    psw_reg.z = FIELD_EX32(psw, PSW, Z);
    psw_reg.ie = FIELD_EX32(psw, PSW, IE);

    return psw_reg;
}

#endif // RL78_CPU_H 