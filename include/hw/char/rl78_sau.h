#ifndef HW_CHAR_RL78_SAU_H
#define HW_CHAR_RL78_SAU_H

#include "chardev/char-fe.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_RL78_SAU "rl78-sau"
typedef struct SAUState SAUState;
DECLARE_INSTANCE_CHECKER(SAUState, SAU,
                         TYPE_RL78_SAU)

#define SAU_CHANNEL_NUM (4)

struct SAUState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion data0;
    MemoryRegion data1;
    MemoryRegion control;

    QEMUTimer   timer[SAU_CHANNEL_NUM];
    CharFrontend chr[SAU_CHANNEL_NUM];

    uint16_t    trx_data[SAU_CHANNEL_NUM];
    uint16_t    freq_div[SAU_CHANNEL_NUM];
    uint16_t    ssr[SAU_CHANNEL_NUM];
    uint16_t    smr[SAU_CHANNEL_NUM];
    uint16_t    scr[SAU_CHANNEL_NUM];
    uint16_t    se;
    uint16_t    sps;
    uint16_t    so;
    uint16_t    soe;
    uint16_t    sol;
    uint16_t    ssc;

    int64_t     last_send_time;
    int64_t     next_recv_time;
    uint64_t    freq;
    uint        unit;
};

#endif

