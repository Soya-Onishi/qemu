#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/units.h"
#include "hw/rl78/r7f100gxl.h"
#include "hw/sysbus.h"
#include "system/system.h"
#include "system/address-spaces.h"
#include "hw/qdev-properties.h"
#include "qom/object.h"
#include "migration/vmstate.h"

/**
 * RL78 Internal Memory Base
 */
#define R7F100GXL_INSN_FLASH_BASE  0x00000
#define R7F100GXL_DATA_RAM_BASE  0xF3F00

struct R7F100GXLClass {
    /*< private >*/
    DeviceClass parent_class;

    /*< public >*/
    const char* name;
    uint64_t data_ram_size;
    uint64_t insn_flash_size;
};

typedef struct R7F100GXLClass R7F100GXLClass;
DECLARE_CLASS_CHECKERS(R7F100GXLClass, R7F100GXL_MCU, TYPE_R7F100GXL_MCU)

/*
static void register_sau(R7F100GXLState *s, unsigned unit)
{
    SysBusDevice *sau;

    object_initialize_child(OBJECT(s), "sau[*]", &s->sau[unit], TYPE_RL78_SAU);

    sau = SYS_BUS_DEVICE(&s->sau[unit]);
    qdev_prop_set_chr(DEVICE(sau), "chardev[0]", serial_hd(unit*4+0));
    qdev_prop_set_chr(DEVICE(sau), "chardev[1]", serial_hd(unit*4+1));
    qdev_prop_set_chr(DEVICE(sau), "chardev[2]", serial_hd(unit*4+2));
    qdev_prop_set_chr(DEVICE(sau), "chardev[3]", serial_hd(unit*4+3));

    sysbus_realize(sau, &error_abort);

    sysbus_mmio_map_overlap(sau, 0, 0xFFF10, 1);
    sysbus_mmio_map_overlap(sau, 1, 0xFFF44, 1);
    sysbus_mmio_map_overlap(sau, 2, 0xF0100, 1);
}
*/

static void r7f100gxl_realize(DeviceState *dev, Error **errp) 
{
    R7F100GXLState *s = R7F100GXL_MCU(dev);
    R7F100GXLClass *rlc = R7F100GXL_MCU_GET_CLASS(dev);

    MemoryRegion *system = get_system_memory();

    memory_region_init_rom(&s->flash, OBJECT(dev), "flash-insn", 
                           rlc->insn_flash_size, &error_abort);
    memory_region_add_subregion(system, R7F100GXL_INSN_FLASH_BASE, 
                                &s->flash);

    memory_region_init_ram(&s->sram, OBJECT(dev), "sram", 
                    0xFFEE0 - 0xF3F00, &error_abort);

    memory_region_add_subregion(system, R7F100GXL_DATA_RAM_BASE,
                                &s->sram);

    /* Initialize CPU */
    object_initialize_child(OBJECT(s), "cpu", &s->cpu, TYPE_R7F100GXL_CPU);
    qdev_realize(DEVICE(&s->cpu), NULL, &error_abort);

    // register_sau(s, 0);
}

static void r7f100gxl_class_init(ObjectClass *oc, const void *data) 
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    R7F100GXLClass *rlc = R7F100GXL_MCU_CLASS(oc);

    dc->realize = r7f100gxl_realize;

    rlc->insn_flash_size = 512 * KiB;
    rlc->data_ram_size = 48 * KiB;
}

static const TypeInfo r7f100gxl_types[] = {
    {
        .name = TYPE_R7F100GXL_MCU,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(R7F100GXLState),
        .class_size = sizeof(R7F100GXLClass),
        .class_init = r7f100gxl_class_init 
    }
};

DEFINE_TYPES(r7f100gxl_types)