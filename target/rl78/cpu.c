#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "exec/cputlb.h"
#include "exec/page-protection.h"
#include "exec/translation-block.h"
#include "exec/target_page.h"
#include "hw/loader.h"
#include "accel/tcg/cpu-ops.h"

static void rl78_cpu_set_pc(CPUState *cs, vaddr value)
{
    RL78CPU *cpu = RL78_CPU(cs);

    cpu->env.pc = value;
}

static vaddr rl78_cpu_get_pc(CPUState *cs) 
{
    RL78CPU *cpu = RL78_CPU(cs);

    return cpu->env.pc;
}

static TCGTBCPUState rl78_get_tb_cpu_state(CPUState *cs)
{
    CPURL78State *env = cpu_env(cs);

    return (TCGTBCPUState){ .pc = env->pc, .flags = rl78_cpu_pack_psw(env->psw) };
}

static void rl78_cpu_synchronize_from_tb(CPUState *cs, 
                                         const TranslationBlock *tb)
{
    CPURL78State *env = cpu_env(cs);

    env->pc = tb->pc;
}

static void rl78_restore_state_to_opc(CPUState *cs,
                                      const TranslationBlock *tb,
                                      const uint64_t *data)
{
    CPURL78State *env = cpu_env(cs);
    env->pc = data[0];
}

static vaddr rl78_cpu_pointer_wrap(CPUState *cs, int idx, vaddr res, 
                                   vaddr base)
{
    const vaddr lower = res & 0x00FFFF;
    const vaddr upper = base & 0xFF0000;

    return lower | upper;
}

static bool rl78_cpu_has_work(CPUState *cs)
{
    return cpu_test_interrupt(cs, CPU_INTERRUPT_HARD);
}

static int rl78_cpu_mmu_index(CPUState *cs, bool ifunc) 
{
    return 0;
}

static void rl78_cpu_reset_hold(Object *obj, ResetType type)
{
    CPUState *cs = CPU(obj);
    RL78CPUClass *rlc = RL78_CPU_GET_CLASS(cs);
    CPURL78State *env = cpu_env(cs);

    if(rlc->parent_phases.hold) {
        rlc->parent_phases.hold(obj, type);
    }

    uint16_t* resetvec = rom_ptr(0x000000, 2);
    env->pc = *resetvec; 

    env->psw = (RL78PSWReg){
        .cy = 0,
        .isp = 3,
        .rbs = 0,
        .ac = 0,
        .z = 0,
        .ie = 0
    };
    env->es = 0x0F;
    env->cs = 0x00;
}

static ObjectClass *rl78_cpu_class_by_name(const char *cpu_model) 
{
    ObjectClass *oc;
    char* typename;

    oc = object_class_by_name(cpu_model);
    if(oc != NULL && object_class_dynamic_cast(oc, TYPE_RL78_CPU) != NULL) {
        return oc;
    }

    typename = g_strdup_printf(RL78_CPU_TYPE_NAME("%s"), cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);

    return oc;
}

static void rl78_cpu_realize(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    RL78CPUClass *rlc = RL78_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if(local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    qemu_init_vcpu(cs);
    cpu_reset(cs);

    rlc->parent_realize(dev, errp);
}

/*
static void rl78_cpu_set_irq(void *opaque, int no, int request)
{
    TODO: not implemented yet
}
*/

static void rl78_cpu_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    info->endian = BFD_ENDIAN_LITTLE;
    info->mach = bfd_mach_rl78;
    info->print_insn = print_insn_rl78;
}


static bool rl78_cpu_tlb_fill(CPUState *cs, vaddr addr, int size, 
                              MMUAccessType access_type, int mmu_idx, 
                              bool probe, uintptr_t retaddr)
{
    uint32_t address, physical, prot;

    address = physical = addr & TARGET_PAGE_MASK;
    prot = PAGE_RWX;
    tlb_set_page(cs, address, physical, prot, mmu_idx, TARGET_PAGE_SIZE);

    return true;
}

static void rl78_cpu_init(Object *obj)
{
    /**
     * TODO: not implemented yet
     */
}

#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps rl78_sysemu_ops = {
    .has_work = rl78_cpu_has_work,
    .get_phys_page_debug = rl78_cpu_get_phys_page_debug,
};

static const TCGCPUOps rl78_tcg_ops = {
    .guest_default_memory_order = TCG_MO_ALL, 
    .mttcg_supported = false,   

    .initialize = rl78_translate_init,
    .translate_code = rl78_translate_code,
    .get_tb_cpu_state = rl78_get_tb_cpu_state,
    .synchronize_from_tb = rl78_cpu_synchronize_from_tb,
    .restore_state_to_opc = rl78_restore_state_to_opc,
    .mmu_index = rl78_cpu_mmu_index,
    .tlb_fill = rl78_cpu_tlb_fill,
    .pointer_wrap = rl78_cpu_pointer_wrap,
    
    .cpu_exec_interrupt = rl78_cpu_exec_interrupt,
    .cpu_exec_halt = rl78_cpu_has_work,
    .cpu_exec_reset = cpu_reset,
    .do_interrupt = rl78_cpu_do_interrupt,
};

static void rl78_cpu_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    RL78CPUClass *rlc = RL78_CPU_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    device_class_set_parent_realize(dc, rl78_cpu_realize, &rlc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, rl78_cpu_reset_hold, NULL,
                                       &rlc->parent_phases);

    cc->class_by_name = rl78_cpu_class_by_name;
    cc->dump_state = rl78_cpu_dump_state;
    cc->set_pc = rl78_cpu_set_pc;
    cc->get_pc = rl78_cpu_get_pc;

    cc->sysemu_ops = &rl78_sysemu_ops;
    // cc->gdb_read_register = rl78_cpu_gdb_read_register;
    // cc->gdb_write_register = rl78_cpu_gdb_write_register;
    cc->disas_set_info = rl78_cpu_disas_set_info;

    // cc->gdb_core_xml_file = "rl78-core.xml";
    cc->tcg_ops = &rl78_tcg_ops;
}

static const TypeInfo rl78_cpu_info = {
    .name = TYPE_RL78_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(RL78CPU),
    .instance_align = __alignof(RL78CPU),
    .instance_init = rl78_cpu_init,
    .class_size = sizeof(RL78CPUClass),
    .class_init = rl78_cpu_class_init,
};

static void rl78_cpu_register_types(void) 
{
    type_register_static(&rl78_cpu_info);
}

type_init(rl78_cpu_register_types)
