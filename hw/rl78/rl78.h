#ifndef HW_RL78_RL78_H
#define HW_RL78_RL78_H

#include "qom/object.h"
#include "hw/core/sysbus.h"
#include "target/rl78/cpu.h"

#define TYPE_RL78G23_MCU "RL78G23"
#define TYPE_R7F100GXL_MCU "R7F100GxL"

typedef struct RL78G23McuState RL78G23McuState;
DECLARE_INSTANCE_CHECKER(RL78G23McuState, RL78G23_MCU, TYPE_RL78G23_MCU)

struct RL78G23McuState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    RL78CPU cpu;

    MemoryRegion system;
    MemoryRegion control;
    MemoryRegion alias;
};

enum {
    RL78G23_MM_CODE_FLASH,
    RL78G23_MM_EXTENDED_SFR,
    RL78G23_MM_DATA_FLASH,
    RL78G23_MM_MIRROR,
    RL78G23_MM_RAM,
    RL78G23_MM_SFR,
};

#endif
