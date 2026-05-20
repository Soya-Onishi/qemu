#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/cpu.h"
#include "cpu.h"
#include "accel/tcg/cpu-ldst.h"
#include "hw/core/irq.h"
#include "exec/cpu-interrupt.h"
#include "qemu/timer.h"

void rl78_cpu_do_interrupt(CPUState *cs)
{
    CPURL78State *env = cpu_env(cs);
    const uint32_t irq_index = env->irq_index;
    const uint8_t psw = rl78_cpu_pack_psw(env->psw);

    const uint8_t pc_l = (env->pc >>  0) & 0xFF;
    const uint8_t pc_h = (env->pc >>  8) & 0xFF;
    const uint8_t pc_s = (env->pc >> 16) & 0xFF;

    env->sp -= 4;
    const uint32_t stack_addr = 0xF0000 | env->sp;
    cpu_stb_data(env, stack_addr + 0, pc_l);
    cpu_stb_data(env, stack_addr + 1, pc_h);
    cpu_stb_data(env, stack_addr + 2, pc_s);
    cpu_stb_data(env, stack_addr + 3, psw);

    uint32_t vectbl_addr = cs->exception_index * 2 + 4;
    env->psw.ie = 0;
    env->psw.isp = env->irq_priority;
    env->pc = cpu_lduw_data(env, vectbl_addr);
    env->irq_index = -1;
    cs->exception_index = -1;

    qemu_set_irq(env->irq_ack, irq_index);

    /**
     * TODO: implement software break
     */
}

bool rl78_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    CPURL78State *env = cpu_env(cs);

    if(interrupt_request & CPU_INTERRUPT_HARD) {
        // interrupt is enabled ?
        if(!env->psw.ie || env->skip_en) {
            return false;
        }

        if(env->irq_index < 0) {
            cs->exception_index = -1;
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
            return false;
        }

        int priority = env->psw.isp + 1;
        if(env->irq_priority < priority) {
            cs->exception_index = env->irq_index;
            rl78_cpu_do_interrupt(cs);
            return true;
        }
    }

    return false;
}

hwaddr rl78_cpu_get_phys_page_debug(CPUState *cs, vaddr addr) { return addr; }
