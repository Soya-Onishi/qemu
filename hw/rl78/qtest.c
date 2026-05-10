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

    // For SAU
    uint16_t tx_uart_payload[2][4];
    uint8_t tx_uart_stopbits[2][4];
    UartParity tx_uart_parity[2][4];
    
    TransmitPort sau_rx_ports[2][4];
    uint8_t rx_uart_stopbits;
    UartParity rx_uart_parity;
};
typedef struct RL78QTestMachineState RL78QTestMachineState;

#define TYPE_RL78_QTEST_MACHINE MACHINE_TYPE_NAME("qtest")

DECLARE_OBJ_CHECKERS(RL78QTestMachineState, RL78QTestMachineClass,
                     RL78_QTEST_MACHINE, TYPE_RL78_QTEST_MACHINE)


static void set_adc_in(Object *obj, Visitor *v, const char *name, void *opaque,
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

static void get_sau_tx(Object *obj, uint64_t index, const void *payload)
{
    RL78QTestMachineState *s = RL78_QTEST_MACHINE(obj);
    WirePayload *wire_payload = (WirePayload *)payload;

    if(wire_payload->type != WIRE_PAYLOAD_TYPE_SERIAL) {
        return;
    }

    if(wire_payload->serial.type != SERIAL_PACKET_TYPE_UART) {
        return;
    }

    s->tx_uart_payload[0][index] = wire_payload->serial.uart.payload;
    s->tx_uart_stopbits[0][index] = wire_payload->serial.uart.stopbits;
    s->tx_uart_parity[0][index] = wire_payload->serial.uart.parity;
}

static void set_sau_rx(Object *obj, Visitor *v, const char *name, void *opaque,
                      Error **errp)
{
    TransmitPort *port = opaque;
    RL78QTestMachineState *s = RL78_QTEST_MACHINE(obj);
    uint16_t value;

    visit_type_uint16(v, name, &value, errp);
    WirePayload payload = {
        .type = WIRE_PAYLOAD_TYPE_SERIAL,
        .serial = {
            .type = SERIAL_PACKET_TYPE_UART,
            .uart = {
                .payload = value,
                .stopbits = s->rx_uart_stopbits,
                .parity = s->rx_uart_parity,
            },
        },
    };

    qemu_log("set_sau_rx: payload: %d, stopbits: %d, parity: %d\n", value, s->rx_uart_stopbits, s->rx_uart_parity);
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
        object_property_add(OBJECT(machine), name, "double", NULL, set_adc_in, NULL, &s->adc_input_ports[i]);
        g_free(name);

        connect_port(OBJECT(machine), "adc-in-port", i, OBJECT(&s->mcu), "adc_in", i);
    }

    receive_port_add(OBJECT(machine), "sau-tx-port", get_sau_tx, RL78_SAU_CHANNEL_NUM);
    transmit_port_add(OBJECT(machine), "sau-rx-port", s->sau_rx_ports[0], RL78_SAU_CHANNEL_NUM);
    for(int i = 0; i < RL78_SAU_CHANNEL_NUM; i++) {
        char* tx_name = g_strdup_printf("sau-tx[%d]", i);
        char* tx_payload_name = g_strdup_printf("%s.payload", tx_name);
        char* tx_stopbits_name = g_strdup_printf("%s.stopbits", tx_name);
        char* tx_parity_name = g_strdup_printf("%s.parity", tx_name);

        object_property_add_uint16_ptr(OBJECT(machine), tx_payload_name, &s->tx_uart_payload[0][i], OBJ_PROP_FLAG_READWRITE);
        object_property_add_uint8_ptr(OBJECT(machine), tx_stopbits_name, &s->tx_uart_stopbits[0][i], OBJ_PROP_FLAG_READWRITE);
        object_property_add_uint8_ptr(OBJECT(machine), tx_parity_name, (uint8_t*)&s->tx_uart_parity[0][i], OBJ_PROP_FLAG_READWRITE);

        char* rx_name = g_strdup_printf("sau-rx[%d]", i);
        char* rx_payload_name = g_strdup_printf("%s.payload", rx_name);
        char* rx_stopbits_name = g_strdup_printf("%s.stopbits", rx_name);
        char* rx_parity_name = g_strdup_printf("%s.parity", rx_name);

        object_property_add(OBJECT(machine), rx_payload_name, "uint16", NULL, set_sau_rx, NULL, &s->sau_rx_ports[0][i]);
        object_property_add_uint8_ptr(OBJECT(machine), rx_stopbits_name, &s->rx_uart_stopbits, OBJ_PROP_FLAG_WRITE);
        object_property_add_uint8_ptr(OBJECT(machine), rx_parity_name, (uint8_t*)&s->rx_uart_parity, OBJ_PROP_FLAG_WRITE);

        g_free(tx_name);
        g_free(tx_payload_name);
        g_free(tx_stopbits_name);
        g_free(tx_parity_name);
        g_free(rx_name);
        g_free(rx_payload_name);
        g_free(rx_stopbits_name);
        g_free(rx_parity_name);

        connect_port(OBJECT(&s->mcu), "sau[0]_tx", i, OBJECT(machine), "sau-tx-port", i);
        connect_port(OBJECT(machine), "sau-rx-port", i, OBJECT(&s->mcu), "sau[0]_rx", i);
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
