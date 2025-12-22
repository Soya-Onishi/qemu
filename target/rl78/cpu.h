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
REG8(PSW, 0)
FIELD(PSW, CY,   0, 1)
FIELD(PSW, ISP,  1, 2)
FIELD(PSW, RBS0, 3, 1)
FIELD(PSW, AC,   4, 1)
FIELD(PSW, RBS1, 5, 1)
FIELD(PSW, Z,    6, 1)
FIELD(PSW, IE,   7, 1)

enum GPRegister {
    X,
    A,
    C,
    B,
    E,
    D,
    L,
    H,
    GPREG_NUM
};

#define GPREG_BANK_NUM (4)

typedef struct RL78PSWReg {
    bool cy;
    uint8_t isp;
    uint8_t rbs;
    bool ac;
    bool z;
    bool ie;
} RL78PSWReg;

typedef struct CPUArchState {
    /* general purpose register */
    uint8_t regs[GPREG_BANK_NUM][GPREG_NUM]; 

    /* system registers */
    RL78PSWReg psw; /* status register          */
    uint32_t sp;    /* stack pointer            */
    uint32_t pc;    /* program counter          */

    /* registers for upper part of 20bit addresses */
    uint8_t es;
    uint8_t cs;

    /* interrupt info */
    uint8_t req_irq;
    uint8_t req_isp;
    uint8_t ack_irq;
    uint8_t ack_isp;
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

static inline bool rl78_cpu_psw_ie(const uint8_t psw) {
    return FIELD_EX8(psw, PSW, IE);
}

static inline bool rl78_cpu_psw_z(const uint8_t psw) {
    return FIELD_EX8(psw, PSW, Z);
}

static inline uint rl78_cpu_psw_rbs(const uint8_t psw) {
    const uint8_t rbs1 = FIELD_EX8(psw, PSW, RBS1);
    const uint8_t rbs0 = FIELD_EX8(psw, PSW, RBS0);
    const uint8_t rbs = (rbs1 << 1) | (rbs0)

    return rbs;
}

static inline bool rl78_cpu_psw_ac(const uint8_t psw) {
    return FIELD_EX8(psw, PSW, AC);
}

static inline uint rl78_cpu_psw_isp(const uint8_t psw) {
    return FIELD_EX8(psw, PSW, ISP);
}

static inline bool rl78_cpu_psw_cy(const uint8_t  psw) {
    return FIELD_EX8(psw, PSW, CY);
}

#endif // RL78_CPU_H 