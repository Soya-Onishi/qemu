#ifndef HW_RL78_CLOCK_H
#define HW_RL78_CLOCK_H

#include "qemu/typedefs.h"
#include "qom/object.h"
#include "hw/core/sysbus.h"

struct RL78ClockState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public>*/
    MemoryRegion mmio0;
    MemoryRegion mmio1;
    MemoryRegion mmio2;
    MemoryRegion mmio3;

    Clock *fIHP;
    Clock *fIMP;
    Clock *fMXP;
    Clock *fCLK;
    Clock *fMAIN;
    Clock *fIL;
    Clock *fSXP;
    Clock *fRTCCK;

    uint8_t cmc;
    uint8_t ckc;
    uint8_t csc;
    uint8_t ostc;
    uint8_t osts;
    uint8_t cks0;
    uint8_t cks1;
    uint8_t osmc;
    uint8_t cksel;
    uint8_t hocodiv;
    uint8_t mocodiv;
    uint8_t moscdiv;
    uint8_t hiotrm;
    uint8_t miotrm;
    uint8_t liotrm;
    uint8_t wkupmd;

    bool cmc_dirty;
    bool frqsel3;
};
typedef struct RL78ClockState RL78ClockState;

#define TYPE_RL78_CLOCK "rl78-clock"
DECLARE_INSTANCE_CHECKER(RL78ClockState, RL78_CLOCK, TYPE_RL78_CLOCK)

#endif
