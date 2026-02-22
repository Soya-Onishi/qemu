#include "qemu/osdep.h"
#include "cpu.h"
#include "accel/tcg/cpu-ldst.h"
#include "hw/core/irq.h"
#include "qemu/plugin.h"

void rl78_cpu_do_interrupt(CPUState *cs)
{
    /**
     * TODO: implement interrupt handling
     */

    /**
     * TODO: implement software break
     */
}

bool rl78_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    /**
     * TODO: implement
     */

    return false;
}

hwaddr rl78_cpu_get_phys_page_debug(CPUState *cs, vaddr addr) { return addr; }
