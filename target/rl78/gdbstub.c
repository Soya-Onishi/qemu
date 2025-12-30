#include "qemu/osdep.h"
#include "cpu.h"
#include "gdbstub/helpers.h"

const char* rl78_cpu_gdb_arch_name(CPUState *cs)
{
    return "rl78";
}

int rl78_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    CPURL78State *env = cpu_env(cs);

    switch(n) {
    case 0 ... 7:
        return gdb_get_reg8(mem_buf, env->regs[0][n]);
    case 8: {
        const uint8_t psw = rl78_cpu_pack_psw(env->psw);
        return gdb_get_reg8(mem_buf, psw);
    }
    case 9:
        return gdb_get_reg16(mem_buf, env->sp);
    case 10:
        return gdb_get_reg32(mem_buf, env->pc);
    case 11:
        return gdb_get_reg8(mem_buf, env->es);
    case 12:
        return gdb_get_reg8(mem_buf, env->cs);
    }

    return 0;
}

int rl78_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    CPURL78State *env = cpu_env(cs);

    switch(n) {
    case 0 ... 7:
        env->regs[0][n] = ldub_p(mem_buf);
        return 1;
    case 8:
        env->psw = rl78_cpu_unpack_psw(ldub_p(mem_buf));
        return 1;
    case 9:
        env->sp = lduw_p(mem_buf);
        return 2;
    case 10:
        env->pc = ldl_p(mem_buf);
        return 4;
    case 11:
        env->es = ldub_p(mem_buf);
        return 1;
    case 12:
        env->cs = ldub_p(mem_buf);
        return 1;
    }

    return 0;
}