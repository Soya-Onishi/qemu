#include "qemu/osdep.h"
#include "hw/core/boards.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "rl78.h"
#include "boot.h"

struct RL78VirtMachineClass {
    /*< private >*/
    MachineClass parent_class;

    /*< public >*/
    const char *mcu_type;
};
typedef struct RL78VirtMachineClass RL78VirtMachineClass;

struct RL78VirtMachineState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    RL78G23McuState mcu;
};
typedef struct RL78VirtMachineState RL78VirtMachineState;

#define TYPE_RL78_VIRT_MACHINE MACHINE_TYPE_NAME("virt")

DECLARE_OBJ_CHECKERS(RL78VirtMachineState, RL78VirtMachineClass,
                     RL78_VIRT_MACHINE, TYPE_RL78_VIRT_MACHINE)

static void rl78_virt_init(MachineState *machine)
{
    RL78VirtMachineState *s   = RL78_VIRT_MACHINE(machine);
    RL78VirtMachineClass *rlc = RL78_VIRT_MACHINE_GET_CLASS(machine);

    object_initialize_child(OBJECT(machine), "mcu", &s->mcu, rlc->mcu_type);

    if (machine->firmware) {
        if (!rl78_load_firmware(&s->mcu.cpu, machine, &s->mcu.system,
                                machine->firmware)) {
            error_report("Failed to load firmware image %s", machine->firmware);
            exit(1);
        }
    }

    sysbus_realize(SYS_BUS_DEVICE(&s->mcu), &error_abort);
}

static void rl78_virt_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc          = MACHINE_CLASS(oc);
    RL78VirtMachineClass *rlc = RL78_VIRT_MACHINE_CLASS(oc);

    mc->init         = rl78_virt_init;
    mc->default_cpus = 1;
    mc->min_cpus     = mc->default_cpus;
    mc->max_cpus     = mc->default_cpus;
    mc->no_floppy    = 1;
    mc->no_parallel  = 1;
    mc->no_cdrom     = 1;

    mc->desc      = "virtual RL78 board";
    rlc->mcu_type = TYPE_R7F100GXL_MCU;
}

static const TypeInfo rl78_virt_machine_types[] = {{
    .name          = MACHINE_TYPE_NAME("virt"),
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(RL78VirtMachineState),
    .class_size    = sizeof(RL78VirtMachineClass),
    .class_init    = rl78_virt_class_init,
}};

DEFINE_TYPES(rl78_virt_machine_types)
