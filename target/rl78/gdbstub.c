#include "qemu/osdep.h"
#include "cpu.h"
#include "gdbstub/helpers.h"

const char* rl78_cpu_gdb_arch_name(CPUState *cs)
{
    return "rl78";
}

int rl78_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    return 0;
}

int rl78_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{ 
    return 0;
}
