#ifndef RL78_CPU_STATE_H
#define RL78_CPU_STATE_H

#include "cpu.h"

void rl78_register_cpu_state_mmio(MemoryRegion *cpu_state, RL78CPU *cpu,
                                  vaddr base);

#endif
