#include "qemu/osdep.h"
#include "exec/hwaddr.h"
#include "hw/core/sysbus.h"
#include "qapi/error.h"
#include "qom/object.h"

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
    MemMapEntry sfr;
};

typedef struct RL78G23McuClass RL78G23McuClass;

DECLARE_CLASS_CHECKERS(RL78G23McuClass, RL78G23_MCU, TYPE_RL78G23_MCU)

static void rl78g23_realize(DeviceState *dev, Error **errp)
{
    RL78G23McuState *s = RL78G23_MCU(dev);

    MemoryRegion *code_flash   = g_new(MemoryRegion, 1);
    MemoryRegion *extended_sfr = g_new(MemoryRegion, 1);
    MemoryRegion *data_flash   = g_new(MemoryRegion, 1);
    MemoryRegion *mirror       = g_new(MemoryRegion, 1);
    MemoryRegion *ram          = g_new(MemoryRegion, 1);
    MemoryRegion *sfr          = g_new(MemoryRegion, 1);

    memory_region_init_rom(code_flash, OBJECT(dev), "code-flash", 0xC0000,
                           &error_abort);
    memory_region_init(extended_sfr, OBJECT(dev), "extended-sfr", 0x100000);
    memory_region_init(data_flash, OBJECT(dev), "data-flash", 0x100000);
    memory_region_init_alias(mirror, OBJECT(dev), "mirror", code_flash, 0x0000,
                             0x100000);
    memory_region_init_ram(ram, OBJECT(dev), "ram", 0x100000, &error_abort);
    memory_region_init(sfr, OBJECT(dev), "sfr", 0x100000);

    object_initialize_child(OBJECT(s), "cpu", &s->cpu, TYPE_RL78G23_MCU);

    object_property_set_link(OBJECT(&s->cpu), "code-flash", OBJECT(code_flash),
                             &error_abort);
    object_property_set_link(OBJECT(&s->cpu), "extended-sfr",
                             OBJECT(extended_sfr), &error_abort);
    object_property_set_link(OBJECT(&s->cpu), "data-flash", OBJECT(data_flash),
                             &error_abort);
    object_property_set_link(OBJECT(&s->cpu), "mirror", OBJECT(mirror),
                             &error_abort);
    object_property_set_link(OBJECT(&s->cpu), "ram", OBJECT(ram), &error_abort);
    object_property_set_link(OBJECT(&s->cpu), "sfr", OBJECT(sfr), &error_abort);

    sysbus_realize(SYS_BUS_DEVICE(&s->cpu), &error_abort);
}

static void rl78g23_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

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
    rlc->sfr          = r7f100gxl_mm[RL78G23_MM_SFR];
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
