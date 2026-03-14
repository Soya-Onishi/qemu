#ifndef HW_RL78_CLOCK_H
#define HW_RL78_CLOCK_H

#include "qemu/typedefs.h"
#include "qom/object.h"

struct RL78ClockGenerator {
    Clock *high_osc;
    Clock *middle_osc;
    Clock *low_osc;
};
typedef struct RL78ClockGenerator RL78ClockGenerator;

#define TYPE_RL78_CLOCK_GENERATOR "rl78-clock-generator"
DECLARE_INSTANCE_CHECKER(RL78ClockGenerator, RL78_CLOCK_GENERATOR,
                         TYPE_RL78_CLOCK_GENERATOR)

#endif
