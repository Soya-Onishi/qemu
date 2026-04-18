#ifndef HW_RL78_INTERCOMM_H
#define HW_RL78_INTERCOMM_H

#include "qom/object.h"
#include "qemu/notify.h"

typedef enum IntercommDataType {
    INTERCOMM_DATA_TYPE_DIGITAL,
    INTERCOMM_DATA_TYPE_ANALOG,
    INTERCOMM_DATA_TYPE_UART,
} IntercommDataType;

typedef enum IntercommDataUARTParity {
    INTERCOMM_DATA_UART_PARITY_NONE,
    INTERCOMM_DATA_UART_PARITY_ODD,
    INTERCOMM_DATA_UART_PARITY_EVEN,
} IntercommDataUARTParity;

struct IntercommDataDigital {
    bool signal;
};

struct IntercommDataAnalog {
    double signal;
};

struct IntercommDataUART {
    uint8_t *data;
    uint32_t length;
    uint8_t stopbit;
    IntercommDataUARTParity parity;
    bool is_reverse;
};

typedef struct IntercommDataDigital IntercommDataDigital;
typedef struct IntercommDataAnalog IntercommDataAnalog;
typedef struct IntercommDataUART IntercommDataUART;

struct IntercommPayload {
    uint32_t index;
    IntercommDataType kind;
    union {
        IntercommDataDigital digital;
        IntercommDataAnalog analog;
        IntercommDataUART uart;
    };
};
typedef struct IntercommPayload IntercommPayload;

struct IOCTransmitter {
    NotifierList notifylist;
};
typedef struct IOCTransmitter IOCTransmitter;

struct IOCReceiver {
    Notifier notify;
    uint32_t index;
    void *opaque;
};
typedef struct IOCReceiver IOCReceiver;

void register_rx_property(Object *obj, IOCReceiver *receiver,
                          const char *propname);
void register_tx_property(Object *obj, IOCTransmitter *transmitter,
                          const char *propname);

inline static void get_intercomm_payload_digital(IntercommPayload *payload,
                                                 IntercommDataDigital *data)
{
    assert(payload->kind == INTERCOMM_DATA_TYPE_DIGITAL);
    *data = payload->digital;
}

inline static void get_intercomm_payload_analog(IntercommPayload *payload,
                                                IntercommDataAnalog *data)
{
    assert(payload->kind == INTERCOMM_DATA_TYPE_ANALOG);
    *data = payload->analog;
}

inline static void get_intercomm_payload_uart(IntercommPayload *payload,
                                              IntercommDataUART *data)
{
    assert(payload->kind == INTERCOMM_DATA_TYPE_UART);
    *data = payload->uart;
}

#endif
