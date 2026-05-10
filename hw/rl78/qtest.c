#include "qemu/osdep.h"
#include "hw/core/resettable.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev.h"
#include "qemu/typedefs.h"
#include "qom/object.h"
#include "system/reset.h"
#include "qemu/error-report.h"
#include "qemu/notify.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "rl78.h"
#include "boot.h"

struct RL78QTestMachineClass {
    /*< private >*/
    MachineClass parent_class;

    /*< public >*/
    const char *mcu_type;
};
typedef struct RL78QTestMachineClass RL78QTestMachineClass;

struct RL78QTestMachineState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    RL78G23McuState mcu;

    // For ADC
    TransmitPort adc_input_ports[3];
};
typedef struct RL78QTestMachineState RL78QTestMachineState;

#define TYPE_RL78_QTEST_MACHINE MACHINE_TYPE_NAME("qtest")

DECLARE_OBJ_CHECKERS(RL78QTestMachineState, RL78QTestMachineClass,
                     RL78_QTEST_MACHINE, TYPE_RL78_QTEST_MACHINE)

static void set_uint8(Object *obj, Visitor *v, const char *name, void *opaque,
                      Error **errp)
{
    TransmitPort *port = opaque;
    double value;

    visit_type_number(v, name, &value, errp);
    WirePayload payload = {
        .type = WIRE_PAYLOAD_TYPE_ANALOG,
        .analog = {
            .voltage = value,
        },
    };
    transmit_port_payload(port, &payload);
}

static void rl78_qtest_init(MachineState *machine)
{
    RL78QTestMachineState *s   = RL78_QTEST_MACHINE(machine);
    RL78QTestMachineClass *rlc = RL78_QTEST_MACHINE_GET_CLASS(machine);

    object_initialize_child(OBJECT(machine), "mcu", &s->mcu, rlc->mcu_type);

    sysbus_realize(SYS_BUS_DEVICE(&s->mcu), &error_abort);

    transmit_port_add(OBJECT(machine), "adc-in-port", s->adc_input_ports, ARRAY_SIZE(s->adc_input_ports));
    for(int i = 0; i < ARRAY_SIZE(s->adc_input_ports); i++) {
        char* name = g_strdup_printf("adc-in[%d]", i);
        object_property_add(OBJECT(machine), name, "double", NULL, set_uint8, NULL, &s->adc_input_ports[i]);
        g_free(name);

        connect_port(OBJECT(machine), "adc-in-port", i, OBJECT(&s->mcu), "adc_in", i);
    }
}

static void rl78_qtest_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc           = MACHINE_CLASS(oc);
    RL78QTestMachineClass *rlc = RL78_QTEST_MACHINE_CLASS(oc);

    mc->init         = rl78_qtest_init;
    mc->default_cpus = 1;
    mc->min_cpus     = mc->default_cpus;
    mc->max_cpus     = mc->default_cpus;
    mc->no_floppy    = 1;
    mc->no_parallel  = 1;
    mc->no_cdrom     = 1;

    mc->desc      = "RL78 board for QTest";
    rlc->mcu_type = TYPE_R7F100GXL_MCU;
}

static const TypeInfo rl78_qtest_machine_types[] = {{
    .name          = TYPE_RL78_QTEST_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(RL78QTestMachineState),
    .class_size    = sizeof(RL78QTestMachineClass),
    .class_init    = rl78_qtest_class_init,
}};

DEFINE_TYPES(rl78_qtest_machine_types)
