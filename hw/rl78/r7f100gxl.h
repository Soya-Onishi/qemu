#ifndef HW_RL78_RL78_H
#define HW_RL78_RL78_H

#include "target/rl78/cpu.h"
#include "system/memory.h"
#include "qemu/typedefs.h"
#include "qom/object.h"
#include "hw/char/rl78_sau.h"

#define TYPE_R7F100GXL_MCU "r7f100gxl-mcu"

typedef struct R7F100GXLState R7F100GXLState;
DECLARE_INSTANCE_CHECKER(R7F100GXLState, R7F100GXL_MCU, TYPE_R7F100GXL_MCU)

struct R7F100GXLState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    RL78CPU cpu;

    SAUState sau[1];

    MemoryRegion    flash;
    MemoryRegion    sram; 
};

#endif // HW_RL78_RL78_H