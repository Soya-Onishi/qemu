#ifndef RL78_CPU_PARAM_H
#define RL78_CPU_PARAM_H

/**
 * Although the RL78 does not have memory pages, 
 * memory access is limited to 64 KB when not using the ES/CS registers.
 */
#define TARGET_PAGE_BITS 16

#define TARGET_PHYS_ADDR_SPACE_BITS 20
#define TARGET_VIRT_ADDR_SPACE_BITS 20

#define TARGET_INSN_START_EXTRA_WORDS 0

#endif // RL78_CPU_PARAM_H