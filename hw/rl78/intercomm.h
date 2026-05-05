#ifndef HW_RL78_INTERCOMM_H
#define HW_RL78_INTERCOMM_H

#include "qom/object.h"

typedef void (*ReceivePayloadHandler)(Object *instance, uint64_t index,
                                      const void *payload);

struct ReceivePort {
    Object parent_obj;

    Object *instance;
    uint64_t index;
    ReceivePayloadHandler handler;
};
typedef struct ReceivePort ReceivePort;
#define TYPE_RECEIVE_PORT "receive-port"
DECLARE_INSTANCE_CHECKER(ReceivePort, RECEIVE_PORT, TYPE_RECEIVE_PORT)

struct TransmitPort {
    Object parent_obj;

    GList *connections;
};
typedef struct TransmitPort TransmitPort;
#define TYPE_TRANSMIT_PORT "transmit-port"
DECLARE_INSTANCE_CHECKER(TransmitPort, TRANSMIT_PORT, TYPE_TRANSMIT_PORT)

typedef enum WirePayloadType {
    WIRE_PAYLOAD_TYPE_DIGITAL = 0,
    WIRE_PAYLOAD_TYPE_ANALOG,
    WIRE_PAYLOAD_TYPE_SERIAL,
} WirePayloadType;

struct DigitalPayload {
    uint8_t high;
}__attribute__((__packed__));
typedef struct DigitalPayload DigitalPayload;

struct AnalogPayload {
    double voltage;
}__attribute__((__packed__));
typedef struct AnalogPayload AnalogPayload;

typedef enum SerialPacketType {
    SERIAL_PACKET_TYPE_UART = 0,
} SerialPacketType;

typedef enum UartParity {
    UART_PARITY_NONE = 0,
    UART_PARITY_ODD,
    UART_PARITY_EVEN,
} UartParity;

struct UartPacket {
    uint16_t payload;
    uint8_t stopbits;
    uint8_t parity;
}__attribute__((__packed__));
typedef struct UartPacket UartPacket;

struct SerialPacket {
    uint8_t type;
    union {
        UartPacket uart;
    };
}__attribute__((__packed__));
typedef struct SerialPacket SerialPacket;

struct WirePayload {
    uint8_t type;
    union {
        DigitalPayload digital;
        AnalogPayload analog;
        SerialPacket serial;
    };
}__attribute__((__packed__));
typedef struct WirePayload WirePayload;

void transmit_port_add(Object *parent, const char *name,
                       TransmitPort *port_list, uint64_t port_num);
void receive_port_add(Object *parent, const char *name,
                      ReceivePayloadHandler handler, uint64_t port_num);
void transmit_port_payload(TransmitPort *port, void *payload);

char* get_port_name(const char *name, uint64_t index);
void connect_port(Object *source, const char *source_name,
                  uint64_t source_index, Object *sink, const char *sink_name,
                  uint64_t sink_index);
void forward_transmit_port(Object *source, const char *source_port_name,
                           uint64_t source_index, Object *self,
                           const char *port_name, uint64_t port_index);
void forward_receive_port(Object *sink, const char *sink_port_name,
                          uint64_t sink_index, Object *self,
                          const char *port_name, uint64_t port_index);

#endif
