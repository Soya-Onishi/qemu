#ifndef HW_RL78_TAU_H
#define HW_RL78_TAU_H

#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qemu/typedefs.h"

#define RL78_TAU_CHANNEL_NUM (8)

union RL78TAUTimerDataRegister {
    uint16_t word;
    uint8_t bytes[2];
};
typedef union RL78TAUTimerDataRegister RL78TAUTimerDataRegister;

union RL78TAUTimerCounter {
    uint16_t word;
    uint8_t bytes[2];
};
typedef union RL78TAUTimerCounter RL78TAUTimerCounter;

enum RL78TAUTimerMode {
    RL78_TAU_TIMER_MODE_INTERVAL = 0,
    RL78_TAU_TIMER_MODE_CAPTURE,
    RL78_TAU_TIMER_MODE_EVENT_COUNTER,
    RL78_TAU_TIMER_MODE_ONE_COUNT,
    RL78_TAU_TIMER_MODE_CAPTURE_AND_ONE_COUNT,
};
typedef enum RL78TAUTimerMode RL78TAUTimerMode;

enum RL78TAUInputEdge {
    RL78_TAU_INPUT_EDGE_RISING = 0,
    RL78_TAU_INPUT_EDGE_FALLING,
    RL78_TAU_INPUT_EDGE_BOTH_LOWLEVEL,
    RL78_TAU_INPUT_EDGE_BOTH_HIGHLEVEL,
};
typedef enum RL78TAUInputEdge RL78TAUInputEdge;

enum RL78TAUStartTrigger {
    RL78_TAU_START_TRIGGER_SOFTWARE = 0,
    RL78_TAU_START_TRIGGER_INPUT_ACCEPTABLE_EDGE,
    RL78_TAU_START_TRIGGER_INPUT_BOTH_EDGE,
    RL78_TAU_START_TRIGGER_MASTER_CHANNEL,
};
typedef enum RL78TAUStartTrigger RL78TAUStartTrigger;

enum RL78TAUTimerInput {
    RL78_TAU_TIMER_INPUT_TI = 0,
    RL78_TAU_TIMER_INPUT_ELCL,
    RL78_TAU_TIMER_INPUT_FIMP,
    RL78_TAU_TIMER_INPUT_FIL,
    RL78_TAU_TIMER_INPUT_FSUB,
};
typedef enum RL78TAUTimerInput RL78TAUTimerInput;

struct RL78TAUTimerModeRegister {
    RL78TAUTimerMode mode;  // MDmn1-3
    uint8_t timer_behavior; // MDmn0

    RL78TAUInputEdge input_edge; // CISmn0-1
    RL78TAUStartTrigger start_trigger; // STSmn0-2
    bool use_split;                 
    bool is_master;
    bool use_ti;

    uint8_t clock_select;
};
typedef struct RL78TAUTimerModeRegister RL78TAUTimerModeRegister;

struct RL78TAUChannelState {
    RL78TAUTimerDataRegister tdr; 
    RL78TAUTimerCounter tcr;
    bool overflow_occurred;
    
    bool enabled;
    bool high_enabled;
    bool output_enabled;
    bool output;
    bool toggle_output;
    bool is_slave;

    QEMUTimer timer;
    QEMUTimer high_timer;

    RL78TAUTimerInput input_type;
};

typedef struct RL78TAUChannelState RL78TAUChannelState;

struct RL78TAUState {
    /* private */
    SysBusDevice parent_obj;

    /* public */
    MemoryRegion mmio[4];

    Clock* inclk;
    uint32_t clk_divider[4];

    RL78TAUChannelState channel[RL78_TAU_CHANNEL_NUM];
    qemu_irq irqs[RL78_TAU_CHANNEL_NUM];
    qemu_irq high_irqs[RL78_TAU_CHANNEL_NUM];

    uint16_t tps;
    RL78TAUTimerModeRegister tmr[RL78_TAU_CHANNEL_NUM];
};

typedef struct RL78TAUState RL78TAUState;

#define TYPE_RL78_TAU "rl78-tau"
DECLARE_INSTANCE_CHECKER(RL78TAUState, RL78_TAU, TYPE_RL78_TAU)

#endif
