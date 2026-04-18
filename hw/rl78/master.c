#include "qemu/osdep.h"
#include "hw/core/resettable.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/core/qdev.h"
#include "qemu/compiler.h"
#include "qemu/typedefs.h"
#include "qemu/notify.h"
#include "qom/object.h"
#include "qom/qom-qobject.h"
#include "system/reset.h"
#include "qemu/error-report.h"
#include "qemu/notify.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "chardev/char-fe.h"
#include "rl78.h"
#include "boot.h"

struct RL78MasterMachineClass {
    /*< private >*/
    MachineClass parent_class;

    /*< public >*/
    const char *mcu_type;
};
typedef struct RL78MasterMachineClass RL78MasterMachineClass;

struct RL78MasterMachineState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    RL78G23McuState mcu;
    CharFrontend uart_tx;
    Notifier uart_tx_notifier;
};
typedef struct RL78MasterMachineState RL78MasterMachineState;

#define TYPE_RL78_MASTER_MACHINE MACHINE_TYPE_NAME("master")

DECLARE_OBJ_CHECKERS(RL78MasterMachineState, RL78MasterMachineClass,
                     RL78_MASTER_MACHINE, TYPE_RL78_MASTER_MACHINE)

static void rl78_master_transmit_uart(Notifier *notifier, void *data) {
    RL78MasterMachineState *s = container_of(notifier, RL78MasterMachineState, uart_tx_notifier);
    const uint16_t txdata = *(uint16_t *)data;

    if (qemu_chr_fe_backend_connected(&s->uart_tx)) {
        qemu_chr_fe_write(&s->uart_tx, (uint8_t *)&txdata, 1);
    }
}

static void rl78_master_init(MachineState *machine)
{
    RL78MasterMachineState *s   = RL78_MASTER_MACHINE(machine);
    RL78MasterMachineClass *rlc = RL78_MASTER_MACHINE_GET_CLASS(machine);

    object_initialize_child(OBJECT(machine), "mcu", &s->mcu, rlc->mcu_type);

    sysbus_realize(SYS_BUS_DEVICE(&s->mcu), &error_abort);

    Chardev *uart_tx = qemu_chr_find("uart-tx");
    if(uart_tx) {
        qemu_chr_fe_init(&s->uart_tx, uart_tx, &error_abort);
        qemu_chr_fe_set_handlers(&s->uart_tx, NULL, NULL, NULL, NULL, s, NULL, true);
    }
    NotifierList* notifierlist = (NotifierList*)(uintptr_t)object_property_get_uint(OBJECT(&s->mcu), "sau-tx[0]_tx-notifierlist", &error_abort);
    s->uart_tx_notifier.notify = rl78_master_transmit_uart;
    notifier_list_add(notifierlist, &s->uart_tx_notifier);

     
    if (machine->firmware) {
        if (!rl78_load_firmware(machine->firmware)) {
            error_report("Failed to load firmware image %s", machine->firmware);
            exit(1);
        }
    }
}

static void rl78_master_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc           = MACHINE_CLASS(oc);
    RL78MasterMachineClass *rlc = RL78_MASTER_MACHINE_CLASS(oc);

    mc->init         = rl78_master_init;
    mc->default_cpus = 1;
    mc->min_cpus     = mc->default_cpus;
    mc->max_cpus     = mc->default_cpus;
    mc->no_floppy    = 1;
    mc->no_parallel  = 1;
    mc->no_cdrom     = 1;

    mc->desc      = "RL78 board for QTest";
    rlc->mcu_type = TYPE_R7F100GXL_MCU;

}

static const TypeInfo rl78_master_machine_types[] = {{
    .name          = TYPE_RL78_MASTER_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(RL78MasterMachineState),
    .class_size    = sizeof(RL78MasterMachineClass),
    .class_init    = rl78_master_class_init,
}};

DEFINE_TYPES(rl78_master_machine_types)
