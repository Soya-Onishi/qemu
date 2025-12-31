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
    case 0 ... 31:
        return 0;
    case 32: {
        const uint8_t psw = rl78_cpu_pack_psw(env->psw);
        return gdb_get_reg8(mem_buf, psw);
    }
    case 33:
        return gdb_get_reg8(mem_buf, env->es);
    case 34:
        return gdb_get_reg8(mem_buf, env->cs);
    case 35 ... 37:
        return 0;
    case 38:
        return gdb_get_reg8(mem_buf, env->pmc);    // TODO: pmc register
    case 39:
        return gdb_get_reg8(mem_buf, 0);    // TODO: mem register
    case 40:
        return gdb_get_reg32(mem_buf, env->pc);
    case 41:
        return gdb_get_reg16(mem_buf, env->sp);
    case 42 ... 49: {
        const int rbs = env->psw.rbs;
        const int reg = n - 42;

        return gdb_get_reg8(mem_buf, env->regs[rbs][reg]);
    }
    case 50 ... 53: {
        const int rbs = env->psw.rbs;
        const int reg = (n - 50) * 2;
        const uint16_t reg_lo = env->regs[rbs][reg + 0];
        const uint16_t reg_hi = env->regs[rbs][reg + 1];
        const uint16_t ret_reg = reg_lo | (reg_hi << 8);

        return gdb_get_reg16(mem_buf, ret_reg); 
    }
    case 54 ... 85: {
        const int offset = n - 54;
        const int reg = offset % 8;
        const int rbs = offset / 8;

        return gdb_get_reg8(mem_buf, env->regs[rbs][reg]);
    }
    case 86 ... 101: {
        const int offset = n - 86;
        const int reg = (offset % 4) * 2;
        const int rbs = offset / 4;
        const uint16_t reg_lo = env->regs[rbs][reg + 0];
        const uint16_t reg_hi = env->regs[rbs][reg + 1];
        const uint16_t ret_reg = reg_lo | (reg_hi << 8);

        return gdb_get_reg16(mem_buf, ret_reg);
    }
    case 102 ... 117:
        return gdb_get_reg16(mem_buf, 0);
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