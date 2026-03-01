#include "qemu/osdep.h"
#include "exec/hwaddr.h"
#include "exec/target_page.h"
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
    MemMapEntry standard_sfr;
};

typedef struct RL78G23McuClass RL78G23McuClass;

DECLARE_CLASS_CHECKERS(RL78G23McuClass, RL78G23_MCU, TYPE_RL78G23_MCU)

static MemMapEntry align_page_size(const MemMapEntry mm)
{
    const hwaddr aligned_base = ROUND_DOWN(mm.base, TARGET_PAGE_SIZE);
    const hwaddr source_size  = mm.size + mm.base - aligned_base;
    const hwaddr aligned_size = ROUND_UP(source_size, TARGET_PAGE_SIZE);

    return (MemMapEntry){
        .base = aligned_base,
        .size = aligned_size,
    };
}

static void rl78g23_realize(DeviceState *dev, Error **errp)
{
    RL78G23McuState *s   = RL78G23_MCU(dev);
    RL78G23McuClass *rlc = RL78G23_MCU_GET_CLASS(dev);

    MemoryRegion *mr_code_flash = g_new(MemoryRegion, 1);
    MemoryRegion *mr_ram        = g_new(MemoryRegion, 1);
    MemoryRegion *mr_data_flash = g_new(MemoryRegion, 1);
    MemoryRegion *mr_mirror     = g_new(MemoryRegion, 1);

    memory_region_init(&s->system, OBJECT(dev), "system", 1024 * 1024);
    memory_region_init(&s->control, OBJECT(dev), "control", 1024 * 1024);
    memory_region_init(&s->alias, OBJECT(dev), "alias", 1024 * 1024);

    MemMapEntry code_flash = align_page_size(rlc->code_flash);
    memory_region_init_rom(mr_code_flash, OBJECT(dev), "code-flash",
                           code_flash.size, &error_abort);
    memory_region_add_subregion(&s->system, code_flash.base, mr_code_flash);

    MemMapEntry ram = align_page_size(rlc->ram);
    memory_region_init_ram(mr_ram, OBJECT(dev), "ram", ram.size, &error_abort);
    memory_region_add_subregion(&s->system, ram.base, mr_ram);

    MemMapEntry data_flash = align_page_size(rlc->data_flash);
    memory_region_init_ram(mr_data_flash, OBJECT(dev), "data-flash",
                           data_flash.size, &error_abort);
    memory_region_add_subregion(&s->system, data_flash.base, mr_data_flash);

    MemMapEntry mirror = align_page_size(rlc->mirror);
    memory_region_init_alias(mr_mirror, OBJECT(dev), "mirror", mr_code_flash,
                             0x00000, 0x10000);
    memory_region_add_subregion(&s->alias, mirror.base, mr_mirror);

    object_initialize_child(OBJECT(s), "cpu", &s->cpu, TYPE_RL78_CPU);
    object_property_set_link(OBJECT(&s->cpu), RL78_CPU_PROP_MR_SYSTEM,
                             OBJECT(&s->system), &error_abort);
    object_property_set_link(OBJECT(&s->cpu), RL78_CPU_PROP_MR_CONTROL,
                             OBJECT(&s->control), &error_abort);
    object_property_set_link(OBJECT(&s->cpu), RL78_CPU_PROP_MR_ALIAS,
                             OBJECT(&s->alias), &error_abort);

    s->cpu.mirror       = rlc->mirror;
    s->cpu.standard_sfr = rlc->standard_sfr;
    s->cpu.extended_sfr = rlc->extended_sfr;

    qdev_realize(DEVICE(&s->cpu), NULL, &error_abort);
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
