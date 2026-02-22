#ifndef HW_RL78_R7F100GXL_H
#define HW_RL78_R7F100GXL_H

#include "hw/rl78/rl78.h"
#include "exec/hwaddr.h"

static const MemMapEntry r7f100gxl_mm[] = {
    [RL78G23_MM_CODE_FLASH]   = {.base = 0x00000, .size = 0x1FFFF},
    [RL78G23_MM_EXTENDED_SFR] = {.base = 0xF0000, .size = 0x00800},
    [RL78G23_MM_DATA_FLASH]   = {.base = 0xF1000, .size = 0x02000},
    [RL78G23_MM_MIRROR]       = {.base = 0xF3000, .size = 0x08F00},
    [RL78G23_MM_RAM]          = {.base = 0xFBF00, .size = 0x04000},
    [RL78G23_MM_SFR]          = {.base = 0xFFF00, .size = 0x00100},
};

#endif
