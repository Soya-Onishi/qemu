#ifndef HW_RL78_SAU_H
#define HW_RL78_SAU_H

#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qemu/typedefs.h"

#define RL78_SAU_CHANNEL_NUM (4)

struct RL78SAUState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    Clock* inclk;

    qemu_irq irq[RL78_SAU_CHANNEL_NUM];
    qemu_irq irq_err[RL78_SAU_CHANNEL_NUM];

    QEMUTimer tx_timer[RL78_SAU_CHANNEL_NUM];
    NotifierList tx_notify[RL78_SAU_CHANNEL_NUM];
    NotifierList rx_notify[RL78_SAU_CHANNEL_NUM];

    uint32_t fTCLK_hz[RL78_SAU_CHANNEL_NUM];

    MemoryRegion mmio[3];

    uint16_t sps;
    uint16_t smr[RL78_SAU_CHANNEL_NUM];
    uint16_t scr[RL78_SAU_CHANNEL_NUM];
    uint16_t sdr[RL78_SAU_CHANNEL_NUM];
    uint16_t ssr[RL78_SAU_CHANNEL_NUM];
    uint16_t se;
    uint16_t soe;
    uint16_t so;
    uint16_t sol;
    uint16_t ssc;   
};
typedef struct RL78SAUState RL78SAUState;

#define TYPE_RL78_SAU "rl78-sau"
DECLARE_INSTANCE_CHECKER(RL78SAUState, RL78_SAU, TYPE_RL78_SAU)

#endif
