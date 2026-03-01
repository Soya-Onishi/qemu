#include "qemu/osdep.h"
#include "gdbstub/helpers.h"
#include "system/memory.h"
#include "cpu.h"

const char* rl78_cpu_gdb_arch_name(CPUState *cs)
{
    return "rl78";
}

static uint16_t rl78_cpu_read_register(CPUState *cs, uint rbs, uint n, uint size)
{
    const uint reg = size == 1 ? n : n << (size - 1);
    const uint offset = (rbs << 3) + reg;

    const hwaddr addr = 0xFFEE0 + offset;

    uint16_t value;
    AddressSpace *system = cpu_get_address_space(cs, RL78_AS_SYSTEM);
    address_space_read(system, addr, MEMTXATTRS_UNSPECIFIED, &value, size);

    return value;
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
        const uint reg = n - 42; 
        uint16_t value = rl78_cpu_read_register(cs, env->psw.rbs, reg, 1);

        return gdb_get_reg8(mem_buf, (uint8_t)value);
    }
    case 50 ... 53: {
        const uint reg = n - 50;
        const uint16_t value = rl78_cpu_read_register(cs, env->psw.rbs, reg, 2);

        return gdb_get_reg16(mem_buf, value); 
    }
    case 54 ... 85: {
        const int offset = n - 54;
        const int reg = offset % 8;
        const int rbs = offset / 8;
        const uint16_t value = rl78_cpu_read_register(cs, rbs, reg, 1);

        return gdb_get_reg8(mem_buf, (uint8_t)value);
    }
    case 86 ... 101: {
        const uint offset = n - 86;
        const uint reg = (offset % 4);
        const uint rbs = offset / 4;
        const uint16_t value = rl78_cpu_read_register(cs, rbs, reg, 2);

        return gdb_get_reg16(mem_buf, value);
    }
    case 102 ... 117:
        return gdb_get_reg16(mem_buf, 0);
    }

    return 0;

}

int rl78_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    // TODO: implement here
    CPURL78State *env = cpu_env(cs);

    switch(n) {
    case 0 ... 7:
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
