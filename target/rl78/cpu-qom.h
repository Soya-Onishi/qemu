#ifndef RL78_CPU_QOM_H
#define RL78_CPU_QOM_H

#include "hw/core/cpu.h"

#define TYPE_RL78_CPU "rl78-cpu"

OBJECT_DECLARE_CPU_TYPE(RL78CPU, RL78CPUClass, RL78_CPU)

#define RL78_CPU_TYPE_SUFFIX "-" TYPE_RL78_CPU
#define RL78_CPU_TYPE_NAME(model) model RL78_CPU_TYPE_SUFFIX

#endif // RL78_CPU_QOM_H
