#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/cpu-common.h"
#include "exec/helper-proto.h"

/* exception */
static inline G_NORETURN
void raise_exception(CPURL78State *env, int index, uintptr_t retaddr)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = index;
    cpu_loop_exit_restore(cs, retaddr);
}

G_NORETURN void helper_halt(CPURL78State *env)
{
    CPUState *cs = env_cpu(env);

    cs->halted = 1;
    raise_exception(env, EXCP_HLT, 0);
}
