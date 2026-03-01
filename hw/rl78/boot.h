#ifndef HW_RL78_BOOT_H
#define HW_RL78_BOOT_H

#include "hw/core/boards.h"
#include "target/rl78/cpu.h"


bool rl78_load_firmware(RL78CPU *cpu , MachineState *ms, 
                        MemoryRegion *flash, const char *firmware);

#endif // HW_RL78_BOOT_H
