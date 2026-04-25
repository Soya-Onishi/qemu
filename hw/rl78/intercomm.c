#include "qemu/osdep.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "hw/rl78/intercomm.h"

struct TransmitPortClass {
    ObjectClass parent_class;
};
typedef struct TransmitPortClass TransmitPortClass;

struct ReceivePortClass {
    ObjectClass parent_class;
};
typedef struct ReceivePortClass ReceivePortClass;

void transmit_port_add(Object *parent, const char *name,
                       TransmitPort *port_list, uint64_t port_num)
{
    for (int i = 0; i < port_num; i++) {
        char *port_name = get_port_name(name, i);

        object_initialize(&port_list[i], sizeof(TransmitPort),
                          TYPE_TRANSMIT_PORT);
        object_property_add_child(parent, port_name, OBJECT(&port_list[i]));
        g_free(port_name);
    }
}

void receive_port_add(Object *parent, const char *name,
                      ReceivePayloadHandler handler, uint64_t port_num)
{
    for (int i = 0; i < port_num; i++) {
        char *port_name   = get_port_name(name, i);
        ReceivePort *port = RECEIVE_PORT(object_new(TYPE_RECEIVE_PORT));
        port->instance    = parent;
        port->handler     = handler;
        port->index       = i;

        object_property_add_child(parent, port_name, OBJECT(port));
        g_free(port_name);
    }
}

void transmit_port_payload(TransmitPort *port, void *payload)
{
    for (GList *it = port->connections; it != NULL; it = it->next) {
        ReceivePort *receive = it->data;
        receive->handler(receive->instance, receive->index, payload);
    }
}

char* get_port_name(const char *name, uint64_t index) {
    return g_strdup_printf("%s[%ld]", name, index);
}

void connect_port(Object *source, const char *source_name, uint64_t source_index, Object *sink,
                  const char *sink_name, uint64_t sink_index)
{
    char *source_indexed_name = get_port_name(source_name, source_index);
    char *sink_indexed_name = get_port_name(sink_name, sink_index);

    Object *source_obj =
        object_property_get_link(source, source_indexed_name, &error_abort);
    Object *sink_obj = object_property_get_link(sink, sink_indexed_name, &error_abort);
    TransmitPort *transmit = TRANSMIT_PORT(source_obj);
    ReceivePort *receive   = RECEIVE_PORT(sink_obj);

    transmit->connections = g_list_append(transmit->connections, receive);

    g_free(source_indexed_name);
    g_free(sink_indexed_name);
}

void forward_transmit_port(Object *source, const char *source_port_name,
                           uint64_t source_index, Object *self,
                           const char *port_name, uint64_t port_index)
{
    char *source_name = get_port_name(source_port_name, source_index);
    char *self_name = get_port_name(port_name, port_index);

    object_property_add_alias(self, self_name, source, source_name);

    g_free(source_name);
    g_free(self_name);
}

void forward_receive_port(Object *sink, const char *sink_port_name,
                          uint64_t sink_index, Object *self,
                          const char *port_name, uint64_t port_index)
{
    char *sink_name = get_port_name(sink_port_name, sink_index);
    char *self_name = get_port_name(port_name, port_index);

    object_property_add_alias(self, self_name, sink, sink_name);

    g_free(sink_name);
    g_free(self_name);
}

static void transmit_port_init(Object *obj)
{
    TransmitPort *port = TRANSMIT_PORT(obj);
    port->connections  = NULL;
}

static void receive_port_init(Object *obj)
{
    ReceivePort *port = RECEIVE_PORT(obj);
    port->instance    = NULL;
    port->index       = 0;
    port->handler     = NULL;
}

static const TypeInfo port_types[] = {
    {
        .name          = TYPE_TRANSMIT_PORT,
        .parent        = TYPE_OBJECT,
        .instance_size = sizeof(TransmitPort),
        .instance_init = transmit_port_init,
        .class_size    = sizeof(TransmitPortClass),
    },
    {
        .name          = TYPE_RECEIVE_PORT,
        .parent        = TYPE_OBJECT,
        .instance_size = sizeof(ReceivePort),
        .instance_init = receive_port_init,
        .class_size    = sizeof(ReceivePortClass),
    }};

DEFINE_TYPES(port_types)
