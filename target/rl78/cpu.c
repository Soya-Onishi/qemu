#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/cpu-common.h"
#include "exec/cpu-interrupt.h"
#include "exec/cputlb.h"
#include "exec/target_page.h"
#include "exec/translation-block.h"
#include "accel/tcg/cpu-ops.h"
#include "tcg/tcg-mo.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/loader.h"
#include "hw/core/cpu.h"

enum RL78TBFlags {
    TB_FLAG_SKIP = 1 << 0,
};

static void rl78_cpu_set_pc(CPUState *cs, vaddr value)
{
    RL78CPU *cpu = RL78_CPU(cs);
    cpu->env.pc  = value;
}

static vaddr rl78_cpu_get_pc(CPUState *cs)
{
    RL78CPU *cpu = RL78_CPU(cs);
    return cpu->env.pc;
}

static TCGTBCPUState rl78_get_tb_cpu_state(CPUState *cs)
{
    CPURL78State *env = cpu_env(cs);
    uint32_t flags    = 0;

    if (env->skip_en) {
        flags |= TB_FLAG_SKIP;
    }

    return (TCGTBCPUState){.pc = env->pc, .flags = flags};
}

static void rl78_cpu_synchronize_from_tb(CPUState *cs,
                                         const TranslationBlock *tb)
{
    CPURL78State *env = cpu_env(cs);

    env->pc = tb->pc;
}

static void rl78_restore_state_to_opc(CPUState *cs, const TranslationBlock *tb,
                                      const uint64_t *data)
{
    RL78CPU *cpu = RL78_CPU(cs);
    cpu->env.pc  = data[0];
}

static vaddr rl78_cpu_pointer_wrap(CPUState *cs, int mmu_idx, vaddr result,
                                   vaddr base)
{
    const vaddr lower = result & 0x00FFFF;
    const vaddr upper = result & 0xFF0000;

    return lower | upper;
}

static bool rl78_cpu_has_work(CPUState *cs)
{
    return cpu_test_interrupt(cs, CPU_INTERRUPT_HARD);
}

static int rl78_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    return RL78_AS_SYSTEM;
}

static void rl78_cpu_reset_hold(Object *obj, ResetType type)
{
    CPUState *cs      = CPU(obj);
    CPURL78State *env = cpu_env(cs);
    RL78CPUClass *rlc = RL78_CPU_GET_CLASS(cs);

    RL78PSW psw = {
        .cy  = 0,
        .isp = 3,
        .rbs = 0,
        .ac  = 0,
        .z   = 0,
        .ie  = 0,
    };

    if (rlc->parent_phases.hold) {
        rlc->parent_phases.hold(obj, type);
    }

    AddressSpace *as   = cpu_get_address_space(cs, RL78_AS_SYSTEM);
    uint16_t *resetvec = rom_ptr_for_as(as, 0x000000, 2);
    if (resetvec) {
        env->pc = lduw_le_p(resetvec);
    }

    env->psw = psw;
    env->es  = 0x0F;
    env->cs  = 0x00;

    env->skip_en  = 0;
    env->skip_req = 0;
}

static ObjectClass *rl78_cpu_class_by_name(const char *cpu_model)
{
    return object_class_by_name(cpu_model);
}

static void rl78_cpu_realize(DeviceState *dev, Error **errp)
{
    CPUState *cs      = CPU(dev);
    RL78CPUClass *rlc = RL78_CPU_GET_CLASS(dev);
    Error *local_err  = NULL;

    cpu_address_space_init(cs, RL78_AS_SYSTEM, "system", cs->memory);
    cpu_address_space_init(cs, RL78_AS_CONTROL, "control", cs->memory);
    cpu_address_space_init(cs, RL78_AS_ALIAS, "alias", cs->memory);

    cpu_exec_realizefn(cs, &local_err);

    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    qemu_init_vcpu(cs);
    cpu_reset(cs);

    rlc->parent_realize(dev, errp);
}

static void rl78_cpu_disas_set_info(const CPUState *cs, disassemble_info *info)
{
    info->endian     = BFD_ENDIAN_LITTLE;
    info->mach       = bfd_mach_rl78;
    info->print_insn = print_insn_rl78;
}

static bool rl78_cpu_tlb_fill(CPUState *cs, vaddr addr, int size,
                              MMUAccessType access_type, int mmu_idx,
                              bool probe, uintptr_t retaddr)
{
    const uint32_t address = addr & TARGET_PAGE_MASK;
    const uint32_t prot    = PAGE_READ | PAGE_WRITE | PAGE_EXEC;

    tlb_set_page(cs, address, address, prot, mmu_idx, size);

    return true;
}

static void rl78_cpu_init(Object *obj)
{
    RL78CPU *cpu = RL78_CPU(obj);

    object_property_add_link(obj, RL78_CPU_PROP_MR_SYSTEM, TYPE_MEMORY_REGION,
                             (Object **)&cpu->system,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
    object_property_add_link(obj, RL78_CPU_PROP_MR_CONTROL, TYPE_MEMORY_REGION,
                             (Object **)&cpu->control,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
    object_property_add_link(
        obj, RL78_CPU_PROP_MR_ALIAS, TYPE_MEMORY_REGION, (Object **)&cpu->alias,
        qdev_prop_allow_set_link_before_realize, OBJ_PROP_LINK_STRONG);
}

#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps rl78_sysemu_ops = {
    .has_work            = rl78_cpu_has_work,
    .get_phys_page_debug = rl78_cpu_get_phys_page_debug,
};

static const TCGCPUOps rl78_tcg_ops = {
    .guest_default_memory_order = TCG_MO_ALL,
    .mttcg_supported            = false,

    .initialize           = rl78_translate_init,
    .translate_code       = rl78_translate_code,
    .get_tb_cpu_state     = rl78_get_tb_cpu_state,
    .synchronize_from_tb  = rl78_cpu_synchronize_from_tb,
    .restore_state_to_opc = rl78_restore_state_to_opc,
    .mmu_index            = rl78_cpu_mmu_index,
    .tlb_fill             = rl78_cpu_tlb_fill,
    .pointer_wrap         = rl78_cpu_pointer_wrap,

    .cpu_exec_interrupt = rl78_cpu_exec_interrupt,
    .cpu_exec_halt      = rl78_cpu_has_work,
    .cpu_exec_reset     = cpu_reset,
    .do_interrupt       = rl78_cpu_do_interrupt,
};

static void rl78_cpu_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc     = DEVICE_CLASS(oc);
    CPUClass *cc        = CPU_CLASS(oc);
    RL78CPUClass *rlc   = RL78_CPU_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    device_class_set_parent_realize(dc, rl78_cpu_realize, &rlc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, rl78_cpu_reset_hold, NULL,
                                       &rlc->parent_phases);

    cc->class_by_name = rl78_cpu_class_by_name;
    cc->dump_state    = rl78_cpu_dump_state;
    cc->set_pc        = rl78_cpu_set_pc;
    cc->get_pc        = rl78_cpu_get_pc;

    cc->max_as         = RL78_AS_NUM;
    cc->sysemu_ops     = &rl78_sysemu_ops;
    cc->disas_set_info = rl78_cpu_disas_set_info;

    cc->gdb_read_register  = rl78_cpu_gdb_read_register;
    cc->gdb_write_register = rl78_cpu_gdb_write_register;
    cc->gdb_arch_name      = rl78_cpu_gdb_arch_name;
    cc->gdb_core_xml_file  = "rl78-core.xml";

    cc->tcg_ops = &rl78_tcg_ops;
}

static const TypeInfo rl78_cpu_info = {
    .name           = TYPE_RL78_CPU,
    .parent         = TYPE_CPU,
    .instance_size  = sizeof(RL78CPU),
    .instance_align = __alignof(RL78CPU),
    .instance_init  = rl78_cpu_init,
    .class_size     = sizeof(RL78CPUClass),
    .class_init     = rl78_cpu_class_init,
};

static void rl78_cpu_register_types(void)
{
    type_register_static(&rl78_cpu_info);
}

type_init(rl78_cpu_register_types)
