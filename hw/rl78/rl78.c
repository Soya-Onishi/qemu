#include "qemu/osdep.h"
#include "exec/hwaddr.h"
#include "exec/target_page.h"
#include "hw/core/sysbus.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "system/address-spaces.h"

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
};

typedef struct RL78G23McuClass RL78G23McuClass;

DECLARE_CLASS_CHECKERS(RL78G23McuClass, RL78G23_MCU, TYPE_RL78G23_MCU)

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
        memory_region_init_ram(&s->ram_first, OBJECT(dev), "ram_first", first_size,
                               &error_abort);
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
                             0x00000, rlc->mirror.size);
    memory_region_add_subregion_overlap(get_system_memory(), rlc->mirror.base,
                                        &s->mirror, 1);

    object_initialize_child(OBJECT(s), "cpu", &s->cpu, TYPE_RL78_CPU);

    qdev_realize(DEVICE(&s->cpu), NULL, &error_abort);

    rl78_register_cpu_state_mmio(&s->cpu_state, &s->cpu, 0xFFFF0);
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
