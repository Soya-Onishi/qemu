#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/rl78/r7f100gxl.h"
#include "hw/loader.h"
#include "hw/boards.h"
#include "boot.h"

struct RL78GdbSimMachineClass {
    /*< private >*/
    MachineClass parent_class;

    /*< public >*/
    const char *mcu_name;
};

struct RL78GdbSimMachineState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    R7F100GXLState mcu;
};

typedef struct RL78GdbSimMachineClass RL78GdbSimMachineClass;
typedef struct RL78GdbSimMachineState RL78GdbSimMachineState;

#define TYPE_RL78_GDBSIM_MACHINE MACHINE_TYPE_NAME("rl78-common")
DECLARE_OBJ_CHECKERS(RL78GdbSimMachineState, RL78GdbSimMachineClass, 
                     RL78_GDBSIM_MACHINE, TYPE_RL78_GDBSIM_MACHINE)


static void rl78_gdbsim_init(MachineState *machine)
{
    RL78GdbSimMachineState *s = RL78_GDBSIM_MACHINE(machine);
    RL78GdbSimMachineClass *rlc = RL78_GDBSIM_MACHINE_GET_CLASS(machine);

    // MemoryRegion *system = get_system_memory();

    /* Allocate memory space */
    // memory_region_add_subregion(system, 0xFBF00, machine->ram);

    /* Initialize MCU */
    object_initialize_child(OBJECT(machine), "mcu", &s->mcu, rlc->mcu_name); 

    if(machine->firmware) {
        if(!rl78_load_firmware(&s->mcu.cpu, machine, 
                               &s->mcu.flash, machine->firmware)) {
            exit(1);
        }
    }

    qdev_realize(DEVICE(&s->mcu), NULL, &error_abort);
}

static void rl78_gdbsim_machine_class_init(ObjectClass *oc, const void *data) 
{
    MachineClass *mc = MACHINE_CLASS(oc);
    RL78GdbSimMachineClass *rlc = RL78_GDBSIM_MACHINE_CLASS(oc);

    rlc->mcu_name = TYPE_R7F100GXL_MCU;
    mc->default_cpu_type = TYPE_R7F100GXL_CPU;
    mc->desc = "gdb simulator (R7F100GxL MCU)";

    mc->init = rl78_gdbsim_init;

    mc->default_ram_size = 48 * KiB;
    mc->default_cpus = 1;
    mc->min_cpus = mc->default_cpus;
    mc->max_cpus = mc->default_cpus;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
}

static const TypeInfo rl78_gdbsim_types[] = {
    {
        .name = TYPE_RL78_GDBSIM_MACHINE,
        .parent = TYPE_MACHINE,
        .instance_size = sizeof(RL78GdbSimMachineState),
        .class_size = sizeof(RL78GdbSimMachineClass),
        .class_init = rl78_gdbsim_machine_class_init,
    }
};

DEFINE_TYPES(rl78_gdbsim_types)
