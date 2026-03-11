#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "qemu/rcu.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "cpu-state.h"
#include "cpu.h"

static void mulhu(CPURL78State *env, uint32_t *regs) 
{
    const uint32_t ax = (regs[RL78_BYTE_REG_A] << 8) | regs[RL78_BYTE_REG_X];
    const uint32_t bc = (regs[RL78_BYTE_REG_B] << 8) | regs[RL78_BYTE_REG_C];
    const uint32_t bcax = bc * ax;

    regs[RL78_BYTE_REG_X] = (bcax >> 0) & 0xFF;
    regs[RL78_BYTE_REG_A] = (bcax >> 8) & 0xFF;
    regs[RL78_BYTE_REG_C] = (bcax >> 16) & 0xFF;
    regs[RL78_BYTE_REG_B] = (bcax >> 24) & 0xFF;
}

static void mulh(CPURL78State *env , uint32_t *regs)
{
    const uint32_t ax = (regs[RL78_BYTE_REG_A] << 8) | regs[RL78_BYTE_REG_X];
    const uint32_t bc = (regs[RL78_BYTE_REG_B] << 8) | regs[RL78_BYTE_REG_C];
    const int32_t signed_ax = (int16_t)ax;
    const int32_t signed_bc = (int16_t)bc;
    const uint32_t bcax = (uint32_t)(signed_bc * signed_ax);

    regs[RL78_BYTE_REG_X] = (bcax >> 0) & 0xFF;
    regs[RL78_BYTE_REG_A] = (bcax >> 8) & 0xFF;
    regs[RL78_BYTE_REG_C] = (bcax >> 16) & 0xFF;
    regs[RL78_BYTE_REG_B] = (bcax >> 24) & 0xFF;
}

static void divhu(CPURL78State *env, uint32_t *regs)
{
    const uint32_t ax = (regs[RL78_BYTE_REG_A] << 8) | regs[RL78_BYTE_REG_X];
    const uint32_t de = (regs[RL78_BYTE_REG_D] << 8) | regs[RL78_BYTE_REG_E];
    const uint32_t quotient = de == 0 ? 0xFFFF : ax / de;
    const uint32_t remainder = de == 0 ? ax : ax % de;

    regs[RL78_BYTE_REG_X] = (quotient >> 0) & 0xFF;
    regs[RL78_BYTE_REG_A] = (quotient >> 8) & 0xFF;
    regs[RL78_BYTE_REG_E] = (remainder >> 0) & 0xFF;
    regs[RL78_BYTE_REG_D] = (remainder >> 8) & 0xFF;
}

static void divwu(CPURL78State *env, uint32_t *regs)
{
    const uint32_t ax = (regs[RL78_BYTE_REG_A] << 8) | regs[RL78_BYTE_REG_X];
    const uint32_t bc = (regs[RL78_BYTE_REG_B] << 8) | regs[RL78_BYTE_REG_C];
    const uint32_t de = (regs[RL78_BYTE_REG_D] << 8) | regs[RL78_BYTE_REG_E];
    const uint32_t hl = (regs[RL78_BYTE_REG_H] << 8) | regs[RL78_BYTE_REG_L];

    const uint32_t bcax = (bc << 16) | ax;
    const uint32_t hlde = (hl << 16) | de;

    const uint32_t quotient  = hlde == 0 ? 0xFFFFFFFF : bcax / hlde;
    const uint32_t remainder = hlde == 0 ? bcax : bcax % hlde;

    regs[RL78_BYTE_REG_X] = (quotient >> 0) & 0xFF;
    regs[RL78_BYTE_REG_A] = (quotient >> 8) & 0xFF;
    regs[RL78_BYTE_REG_C] = (quotient >> 16) & 0xFF;
    regs[RL78_BYTE_REG_B] = (quotient >> 24) & 0xFF;

    regs[RL78_BYTE_REG_E] = (remainder >> 0) & 0xFF;
    regs[RL78_BYTE_REG_D] = (remainder >> 8) & 0xFF;
    regs[RL78_BYTE_REG_L] = (remainder >> 16) & 0xFF;
    regs[RL78_BYTE_REG_H] = (remainder >> 24) & 0xFF;
}

static void exec_mul_div(CPURL78State *env, uint8_t val) 
{
    uint8_t rbs = env->psw.rbs;

    switch(val) {
        case 0x01:
            mulhu(env, env->regs[rbs]);
            break;
        case 0x02:
            mulh(env, env->regs[rbs]);
            break;
        case 0x03:
            divhu(env, env->regs[rbs]);
            break;
        case 0x0B:
            divwu(env, env->regs[rbs]);
            break;
        case 0x05:
        case 0x06:
            // TODO: not implemented error assert
        default:
            // TODO: raise implementation error assert
            break;
    }
}

static void cpu_state_write_impl(CPURL78State *env, hwaddr offset,
                                 uint8_t val) {
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
  case 0x0B:
    exec_mul_div(env, val);
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
                            unsigned size) {
  RL78CPU *cpu = RL78_CPU(opaque);
  for (int i = 0; i < size; i++) {
    const uint8_t byte = val >> (i * 8);
    cpu_state_write_impl(&cpu->env, offset + i, byte);
  }
}

static uint16_t cpu_state_read_impl(CPURL78State *env, hwaddr offset) {
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

static uint64_t cpu_state_read(void *opaque, hwaddr offset, unsigned size) {
  RL78CPU *cpu = RL78_CPU(opaque);
  uint64_t val = 0;
  for (int i = 0; i < size; i++) {
    val |= cpu_state_read_impl(&cpu->env, offset + i) << (i * 8);
  }
  return val;
}

static const MemoryRegionOps cpu_state_ops = {
    .write = cpu_state_write,
    .read = cpu_state_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.max_access_size = 2,
    .valid.max_access_size = 2,
};

void rl78_register_cpu_state_mmio(MemoryRegion *cpu_state, RL78CPU *cpu,
                                  vaddr base) {
  memory_region_init_io(cpu_state, OBJECT(cpu), &cpu_state_ops, cpu,
                        "cpu-state", 0x10);
  memory_region_add_subregion(get_system_memory(), base, cpu_state);
}
