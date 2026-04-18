#include "qemu/osdep.h"
#include "qom/object.h"
#include "qemu/notify.h"
#include "qapi/visitor.h"
#include "intercomm.h"

static void property_get_notifier(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    uint64_t notifier = (uintptr_t)opaque;
    visit_type_uint64(v, name, &notifier, errp); 
}

static void property_get_notifierlist(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    uint64_t notifierlist = (uintptr_t)opaque;
    visit_type_uint64(v, name, &notifierlist, errp);
}

void register_rx_property(Object *obj, IOCReceiver* receiver, const char* propname)  {
    char* notifier_name = g_strdup_printf("%s_rx-notifier", propname);
    object_property_add(obj, notifier_name, "notifier", property_get_notifier, NULL, NULL, &receiver->notify);
    g_free(notifier_name);
}

void register_tx_property(Object *obj, IOCTransmitter* transmitter, const char* propname)  {
    char* notifierlist_name = g_strdup_printf("%s_tx-notifierlist", propname);
    object_property_add(obj, notifierlist_name, "notifierlist", property_get_notifierlist, NULL, NULL, &transmitter->notifylist);
    g_free(notifierlist_name);
}

