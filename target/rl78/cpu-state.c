#include "qemu/osdep.h"
#include "system/memory.h"
#include "system/address-spaces.h"
#include "hw/core/sysbus.h"
#include "cpu.h"
#include "cpu-state.h"

static void cpu_state_write_impl(CPURL78State *env, hwaddr offset, uint8_t val)
{
    switch (offset) {
    case 0x00:
    case 0x02:
        // TODO: support MACR registers
        break;
    case 0x08:
        env->sp &= 0xFF00;
        env->sp |= (uint16_t)(val & 0xFE);
        break;
    case 0x09:
        env->sp &= 0x00FF;
        env->sp |= (uint16_t)val << 8;
        break;
    case 0x0A:
        env->psw = rl78_cpu_unpack_psw(val);
        break;
    case 0x0C:
        env->cs = val & 0x0F;
        break;
    case 0x0D:
        env->es = val & 0x0F;
        break;
    case 0x0E:
        env->pmc = val & 0x01;
        break;
    case 0x0F:
        env->mem = val & 0xFF;
        break;
    }
}

static void cpu_state_write(void *opaque, hwaddr offset, uint64_t val,
                            unsigned size)
{
    RL78CPU *cpu = RL78_CPU(opaque);
    for (int i = 0; i < size; i++) {
        const uint8_t byte = val >> (i * 8);
        cpu_state_write_impl(&cpu->env, offset + i, byte);
    }
}

static uint16_t cpu_state_read_impl(CPURL78State *env, hwaddr offset)
{
    switch (offset) {
    case 0x00:
    case 0x02:
        // TODO: support MACR registers
        return 0;
    case 0x08:
        return env->sp & 0x00FF;
    case 0x09:
        return (env->sp & 0xFF00) >> 8;
    case 0x0A:
        return rl78_cpu_pack_psw(env->psw);
    case 0x0C:
        return env->cs & 0x0F;
    case 0x0D:
        return env->es & 0x0F;
    case 0x0E:
        return env->pmc & 0x01;
    case 0x0F:
        return env->mem & 0xFF;
    }

    return 0;
}

static uint64_t cpu_state_read(void *opaque, hwaddr offset, unsigned size)
{
    RL78CPU *cpu = RL78_CPU(opaque);
    uint64_t val = 0;
    for (int i = 0; i < size; i++) {
        val |= cpu_state_read_impl(&cpu->env, offset + i) << (i * 8);
    }
    return val;
}

static const MemoryRegionOps cpu_state_ops = {
    .write                 = cpu_state_write,
    .read                  = cpu_state_read,
    .endianness            = DEVICE_LITTLE_ENDIAN,
    .impl.max_access_size  = 2,
    .valid.max_access_size = 2,
};

void rl78_register_cpu_state_mmio(MemoryRegion *cpu_state, RL78CPU *cpu, vaddr base)
{
    memory_region_init_io(cpu_state, OBJECT(cpu), &cpu_state_ops, cpu,
                          "cpu-state", 0x10);
    memory_region_add_subregion(get_system_memory(), base, cpu_state);
}
