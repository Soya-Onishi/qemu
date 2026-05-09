#include "qemu/osdep.h"
#include "qemu/log.h"
#include "exec/hwaddr.h"
#include "exec/target_page.h"
#include "hw/core/resettable.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-clock.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "system/address-spaces.h"
#include "system/memory.h"

#include "target/rl78/cpu-state.h"

#include "hw/rl78/rl78.h"
#include "hw/rl78/r7f100gxl.h"

struct RL78G23McuClass {
    /*< private >*/
    SysBusDeviceClass parent_class;

    /*< public >*/
    const char *cpu_type;

    MemMapEntry code_flash;
    MemMapEntry extended_sfr;
    MemMapEntry data_flash;
    MemMapEntry mirror;
    MemMapEntry ram;
    MemMapEntry standard_sfr;

    ResettablePhases parent_phases;
};

typedef struct RL78G23McuClass RL78G23McuClass;

DECLARE_CLASS_CHECKERS(RL78G23McuClass, RL78G23_MCU, TYPE_RL78G23_MCU)

static void rl78g23_reset_hold(Object *obj, ResetType type)
{
    RL78G23McuState *s = RL78G23_MCU(obj);

    resettable_reset(OBJECT(&s->cpu), type);
}

static void rl78g23_register_clock(RL78G23McuState *s)
{
    SysBusDevice *clock;

    object_initialize_child(OBJECT(s), "clock", &s->clock, TYPE_RL78_CLOCK);

    clock = SYS_BUS_DEVICE(&s->clock);
    sysbus_realize(clock, &error_abort);

    sysbus_mmio_map(clock, 0, 0xFFFA0);
    sysbus_mmio_map(clock, 1, 0xF00F2);
    sysbus_mmio_map(clock, 2, 0xF00A0);
    sysbus_mmio_map(clock, 3, 0xF0212);
}

static void rl78g23_register_irq(RL78G23McuState *s)
{
    SysBusDevice *irq;

    object_initialize_child(OBJECT(s), "irq", &s->irq,
                            TYPE_RL78_IRQ_CONTROLLER);

    irq = SYS_BUS_DEVICE(&s->irq);
    sysbus_realize(irq, &error_abort);

    sysbus_mmio_map(irq, 0, 0xFFFE0);
    sysbus_mmio_map(irq, 1, 0xFFFD0);
    sysbus_mmio_map(irq, 2, 0xFFF38);

    qemu_irq irq_ack = qdev_get_gpio_in_named(DEVICE(&s->irq), "irq-ack", 0);
    qdev_connect_gpio_out_named(DEVICE(&s->cpu), "irq-ack", 0, irq_ack);

    qemu_irq irq_req = qdev_get_gpio_in_named(DEVICE(&s->cpu), "irq-in", 0);
    qdev_connect_gpio_out_named(DEVICE(&s->irq), "irq-req", 0, irq_req);
}

static void rl78g23_register_gpio(RL78G23McuState *s)
{
    SysBusDevice *gpio;

    object_initialize_child(OBJECT(s), "gpio", &s->gpio, TYPE_RL78_GPIO);

    gpio = SYS_BUS_DEVICE(&s->gpio);
    sysbus_realize(gpio, &error_abort);

    sysbus_mmio_map(gpio, 0, 0xFFF20);
    sysbus_mmio_map(gpio, 1, 0xFFF00);
    sysbus_mmio_map(gpio, 2, 0xF0077);
    sysbus_mmio_map(gpio, 3, 0xF007D);
    sysbus_mmio_map(gpio, 4, 0xF0030);
    sysbus_mmio_map(gpio, 5, 0xF0260);

    for(int i = 0; i < RL78_GPIO_PIN_NUM; i++) {
        forward_transmit_port(OBJECT(&s->gpio), "out", i, OBJECT(s), "gpio_out", i);
        forward_receive_port(OBJECT(&s->gpio), "in", i, OBJECT(s), "gpio_in", i);
    }
}

static void rl78g23_register_sau(RL78G23McuState *s, uint8_t unit)
{
    SysBusDevice *sau;

    // TODO: support multiple SAU units

    object_initialize_child(OBJECT(s), "sau[*]", &s->sau, TYPE_RL78_SAU);
    qdev_connect_clock_in(DEVICE(&s->sau), "inclk", s->clock.fCLK);

    sau = SYS_BUS_DEVICE(&s->sau);
    sysbus_realize(sau, &error_abort);

    sysbus_mmio_map(sau, 0, 0xFFF10);
    sysbus_mmio_map(sau, 1, 0xFFF44);
    sysbus_mmio_map(sau, 2, 0xF0100);

    const RL78CPUIRQ irq_types[RL78_SAU_CHANNEL_NUM] = {
        RL78_CPU_IRQ_INTST0,
        RL78_CPU_IRQ_INTSR0,
        RL78_CPU_IRQ_INTST1,
        RL78_CPU_IRQ_INTSR1,
    };

    for (int i = 0; i < RL78_SAU_CHANNEL_NUM; i++) {
        qemu_irq irq =
            qdev_get_gpio_in_named(DEVICE(&s->irq), "irq-in", irq_types[i]);
        qdev_connect_gpio_out_named(DEVICE(&s->sau), "irq", i, irq);
    }

    qemu_irq irq_sre0 =
        qdev_get_gpio_in_named(DEVICE(&s->irq), "irq-in", RL78_CPU_IRQ_INTSRE0);
    qdev_connect_gpio_out_named(DEVICE(&s->sau), "irq-err", 0, irq_sre0);
    qemu_irq irq_sre1 =
        qdev_get_gpio_in_named(DEVICE(&s->irq), "irq-in", RL78_CPU_IRQ_INTSRE1);
    qdev_connect_gpio_out_named(DEVICE(&s->sau), "irq-err", 1, irq_sre1);

    for (int i = 0; i < RL78_SAU_CHANNEL_NUM; i++) {
        char *rx_name = g_strdup_printf("sau[%d]_rx", unit);
        char *tx_name = g_strdup_printf("sau[%d]_tx", unit);

        forward_receive_port(OBJECT(&s->sau), "rx", i, OBJECT(s), rx_name, i);
        forward_transmit_port(OBJECT(&s->sau), "tx", i, OBJECT(s), tx_name, i);

        g_free(rx_name);
        g_free(tx_name);
    }
}

static void rl78g23_register_tau(RL78G23McuState *s, uint8_t unit)
{
    SysBusDevice *tau;

    object_initialize_child(OBJECT(s), "tau[*]", &s->tau, TYPE_RL78_TAU);
    qdev_connect_clock_in(DEVICE(&s->tau), "inclk", s->clock.fCLK);

    tau = SYS_BUS_DEVICE(&s->tau);
    sysbus_realize(tau, &error_abort);

    sysbus_mmio_map(tau, 0, 0xFFF18);
    sysbus_mmio_map(tau, 1, 0xFFF64);
    sysbus_mmio_map(tau, 2, 0xF0180);
    if (unit == 0) {
        // TIS registers are supported only for TAU unit 0
        sysbus_mmio_map(tau, 3, 0xF0074);
    }

    const RL78CPUIRQ irq_types[RL78_TAU_CHANNEL_NUM] = {
        RL78_CPU_IRQ_INTTM00, RL78_CPU_IRQ_INTTM01, RL78_CPU_IRQ_INTTM02,
        RL78_CPU_IRQ_INTTM03, RL78_CPU_IRQ_INTTM04, RL78_CPU_IRQ_INTTM05,
        RL78_CPU_IRQ_INTTM06, RL78_CPU_IRQ_INTTM07,
    };

    for (int i = 0; i < RL78_TAU_CHANNEL_NUM; i++) {
        qemu_irq irq =
            qdev_get_gpio_in_named(DEVICE(&s->irq), "irq-in", irq_types[i]);
        qdev_connect_gpio_out_named(DEVICE(&s->tau), "irq-out", i, irq);
    }

    qemu_irq high_irq_01 = qdev_get_gpio_in_named(DEVICE(&s->irq), "irq-in",
                                                  RL78_CPU_IRQ_INTTM01H);
    qemu_irq high_irq_03 = qdev_get_gpio_in_named(DEVICE(&s->irq), "irq-in",
                                                  RL78_CPU_IRQ_INTTM03H);
    qdev_connect_gpio_out_named(DEVICE(&s->tau), "irq-out-high", 1,
                                high_irq_01);
    qdev_connect_gpio_out_named(DEVICE(&s->tau), "irq-out-high", 3,
                                high_irq_03);
}

static void rl78g23_register_adc(RL78G23McuState *s)
{
    SysBusDevice *adc;

    object_initialize_child(OBJECT(s), "adc", &s->adc, TYPE_RL78_ADC);
    qdev_connect_clock_in(DEVICE(&s->adc), "inclk", s->clock.fCLK);

    adc = SYS_BUS_DEVICE(&s->adc);
    sysbus_realize(adc, &error_abort);

    sysbus_mmio_map(adc, 0, 0xFFF1E);
    sysbus_mmio_map(adc, 1, 0xFFF30);
    sysbus_mmio_map(adc, 2, 0xF0010);

    qemu_irq irq =
        qdev_get_gpio_in_named(DEVICE(&s->irq), "irq-in", RL78_CPU_IRQ_INTAD);
    qdev_connect_gpio_out_named(DEVICE(adc), "irq-out", 0, irq);

    for(int i = 0; i < ARRAY_SIZE(s->adc.adc_results); i++) {
        forward_receive_port(OBJECT(&s->adc), "in", i, OBJECT(s), "adc_in", i); 
    }
}

static void rl78g23_realize(DeviceState *dev, Error **errp)
{
    RL78G23McuState *s   = RL78G23_MCU(dev);
    RL78G23McuClass *rlc = RL78G23_MCU_GET_CLASS(dev);

    memory_region_init_rom(&s->code_flash, OBJECT(dev), "code-flash",
                           rlc->code_flash.size, &error_abort);
    memory_region_add_subregion(get_system_memory(), rlc->code_flash.base,
                                &s->code_flash);

    const hwaddr first_size =
        ROUND_UP(rlc->ram.base, TARGET_PAGE_SIZE) - rlc->ram.base;
    if (first_size) {
        memory_region_init_ram(&s->ram_first, OBJECT(dev), "ram_first",
                               first_size, &error_abort);
        memory_region_add_subregion(get_system_memory(), rlc->ram.base,
                                    &s->ram_first);
    }
    memory_region_init_ram(&s->ram_remain, OBJECT(dev), "ram",
                           rlc->ram.size - first_size, &error_abort);
    memory_region_add_subregion(get_system_memory(),
                                ROUND_UP(rlc->ram.base, TARGET_PAGE_SIZE),
                                &s->ram_remain);

    memory_region_init(&s->data_flash, OBJECT(dev), "data-flash",
                       rlc->data_flash.size);
    memory_region_add_subregion(get_system_memory(), rlc->data_flash.base,
                                &s->data_flash);

    memory_region_init_alias(&s->mirror, OBJECT(dev), "mirror", &s->code_flash,
                             0x3000, rlc->mirror.size);
    memory_region_add_subregion(get_system_memory(), rlc->mirror.base,
                                &s->mirror);

    object_initialize_child(OBJECT(s), "cpu", &s->cpu, TYPE_RL78_CPU);

    qdev_realize(DEVICE(&s->cpu), NULL, &error_abort);

    rl78_register_cpu_state_mmio(&s->cpu_state, &s->cpu, 0xFFFF0);

    // register peripherals
    rl78g23_register_clock(s);
    rl78g23_register_irq(s);
    rl78g23_register_gpio(s);
    rl78g23_register_sau(s, 0);
    rl78g23_register_tau(s, 0);
    rl78g23_register_adc(s);
}

static void rl78g23_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc      = DEVICE_CLASS(oc);
    RL78G23McuClass *rlc = RL78G23_MCU_CLASS(oc);
    ResettableClass *rc  = RESETTABLE_CLASS(oc);

    resettable_class_set_parent_phases(rc, NULL, rl78g23_reset_hold, NULL,
                                       &rlc->parent_phases);

    dc->realize = rl78g23_realize;
}

static void r7f100gxl_class_init(ObjectClass *oc, const void *data)
{

    RL78G23McuClass *rlc = RL78G23_MCU_CLASS(oc);

    rlc->cpu_type = RL78_CPU_TYPE_NAME("R7F100GxL");

    rlc->code_flash   = r7f100gxl_mm[RL78G23_MM_CODE_FLASH];
    rlc->extended_sfr = r7f100gxl_mm[RL78G23_MM_EXTENDED_SFR];
    rlc->data_flash   = r7f100gxl_mm[RL78G23_MM_DATA_FLASH];
    rlc->mirror       = r7f100gxl_mm[RL78G23_MM_MIRROR];
    rlc->ram          = r7f100gxl_mm[RL78G23_MM_RAM];
    rlc->standard_sfr = r7f100gxl_mm[RL78G23_MM_SFR];
}

static const TypeInfo rl78g23_mcu_types[] = {
    {
        .name       = TYPE_R7F100GXL_MCU,
        .parent     = TYPE_RL78G23_MCU,
        .class_init = r7f100gxl_class_init,
    },
    {
        .name          = TYPE_RL78G23_MCU,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(RL78G23McuState),
        .class_size    = sizeof(RL78G23McuClass),
        .class_init    = rl78g23_class_init,
        .abstract      = true,
    },
};

DEFINE_TYPES(rl78g23_mcu_types)
