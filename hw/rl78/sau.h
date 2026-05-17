#ifndef HW_RL78_SAU_H
#define HW_RL78_SAU_H

#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qemu/typedefs.h"
#include "hw/rl78/intercomm.h"

typedef enum {
    RL78_SAU_START_TRIGGER_SOFTWARE = 0,
    RL78_SAU_START_TRIGGER_PORTEDGE,
} RL78SAUStartTrigger;

typedef enum {
    RL78_SAU_UART_RX_STARTBIT_FALL = 0,
    RL78_SAU_UART_RX_STARTBIT_RISE,
} RL78SAUUARTRXStartBit;

typedef enum {
    RL78_SAU_COMMUNICATION_MODE_SPI = 0,
    RL78_SAU_COMMUNICATION_MODE_UART,
    RL78_SAU_COMMUNICATION_MODE_I2C,
} RL78SAUCommunicationMode;

typedef enum {
    RL78_SAU_TX_INTTYPE_TX_DONE = 0,
    RL78_SAU_TX_INTTYPE_SDR_EMPTY,
} RL78SAUTXIntType;

typedef enum {
    RL78_SAU_UART_PARITY_NONE = 0,
    RL78_SAU_UART_PARITY_ZERO,
    RL78_SAU_UART_PARITY_EVEN,
    RL78_SAU_UART_PARITY_ODD,
} RL78SAUUARTParity;

typedef enum {
    RL78_SAU_BIT_DIRECTION_MSB = 0,
    RL78_SAU_BIT_DIRECTION_LSB,
} RL78SAUBitDirection;

typedef enum {
    RL78_SAU_UART_STOPBITS_0 = 0,
    RL78_SAU_UART_STOPBITS_1,
    RL78_SAU_UART_STOPBITS_2,
} RL78SAUUARTStopBits;

typedef enum {
    RL78_SAU_DATABITS_9 = 1,
    RL78_SAU_DATABITS_7 = 2,
    RL78_SAU_DATABITS_8 = 3,
} RL78SAUDATABitSize;

#define RL78_SAU_CHANNEL_NUM (4)

struct RL78SAUStatus {
    bool is_busy;
    bool is_sdr_dirty;
    bool has_framing_error;
    bool has_parity_error;
    bool has_overflow_error;
};
typedef struct RL78SAUStatus RL78SAUStatus;

struct RL78SAUClock {
    uint8_t ck_select;
    bool use_internal_clock;    
    uint8_t divisor;

    uint64_t fTCLK_hz;
};
typedef struct RL78SAUClock RL78SAUClock;

struct RL78SAUChannel {
    QEMUTimer interval_timer;
    QEMUTimer interval_rx_timer;
    GQueue *rx_data_queue;

    RL78SAUClock clock; 
    
    RL78SAUCommunicationMode communication_mode;

    RL78SAUTXIntType tx_inttype;

    RL78SAUStartTrigger trigger;    

    RL78SAUUARTParity parity;
    RL78SAUUARTStopBits stopbits;
    RL78SAUDATABitSize databits;
    RL78SAUBitDirection bitdirection;
    RL78SAUUARTRXStartBit startbit;

    bool enabled;           // SE bit
    bool output_enabled;    // SOE bit
    bool initial_output;   // SO bit
    bool initial_clock;

    bool tx_enabled;
    bool rx_enabled;
    bool sre_enabled;
    
    bool is_level_inverted;

    uint16_t data;

    // TODO: SSC/ISC/NFEN/ULBS register

    RL78SAUStatus status;
};
typedef struct RL78SAUChannel RL78SAUChannel;

struct RL78SAUState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    Clock *inclk;
    uint8_t ck_divisor[2];
    MemoryRegion mmio[3];

    qemu_irq irqs[RL78_SAU_CHANNEL_NUM];
    qemu_irq irq_errs[RL78_SAU_CHANNEL_NUM];

    TransmitPort tx_ports[RL78_SAU_CHANNEL_NUM];
    
    RL78SAUChannel channels[RL78_SAU_CHANNEL_NUM];
};
typedef struct RL78SAUState RL78SAUState;

#define TYPE_RL78_SAU "rl78-sau"
DECLARE_INSTANCE_CHECKER(RL78SAUState, RL78_SAU, TYPE_RL78_SAU)

#endif
