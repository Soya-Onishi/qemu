#include "qemu/osdep.h"
#include "chardev/char-fe.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev.h"
#include "hw/core/resettable.h"
#include "hw/rl78/intercomm.h"
#include "hw/rl78/boot.h"
#include "qapi/error.h"
#include "qapi/qapi-types-dyn-machine.h"
#include "qapi/qapi-visit-dyn-machine.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/visitor.h"
#include "qemu/aio.h"
#include "qemu/compiler.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/notify.h"
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qemu/typedefs.h"
#include "qobject/qjson.h"
#include "qom/object.h"
#include "qom/qom-qobject.h"
#include "rl78.h"
#include "system/iothread.h"
#include "system/reset.h"

struct IPCSignalPayload {
  uint32_t pin_id;
  uint64_t sequence;
  uint64_t timestamp;
  WirePayload payload;
} __attribute__((__packed__));
typedef struct IPCSignalPayload IPCSignalPayload;

struct IPCPayloadHeader {
  uint32_t machine_id;
  uint8_t payload_count;
} __attribute__((__packed__));
typedef struct IPCPayloadHeader IPCPayloadHeader;

struct RL78DynamicMachineClass {
  /*< private >*/
  MachineClass parent_class;

  /*< public >*/
};
typedef struct RL78DynamicMachineClass RL78DynamicMachineClass;

struct RL78DynamicMachineState {
  /*< private >*/
  MachineState parent_obj;

  /*< public >*/
  RL78G23McuState mcu;

  uint32_t machine_id;
  char *config_path;
  GHashTable *input_connections;

  IOThread *port_thread;
  CharFrontend port_chardev;

  GQueue *outport_pendings;
  QEMUBH *outport_flush_bh;
  QEMUTimer *outport_timer;

  QemuMutex outport_mutex;
  QemuMutex inport_mutex;

  uint64_t *outport_key_table;
  uint64_t *outport_seq_table;

  uint64_t inport_count;
  GHashTable *inport_key_table;
  GByteArray *inport_unhandled_payloads;
  uint64_t* inport_seq_table;
  GList **inport_pending_payloads;

  TransmitPort *inports;
  GList *inport_payloads;
};
typedef struct RL78DynamicMachineState RL78DynamicMachineState;

#define TYPE_RL78_DYNAMIC_MACHINE MACHINE_TYPE_NAME("dynamic")

DECLARE_OBJ_CHECKERS(RL78DynamicMachineState, RL78DynamicMachineClass,
                     RL78_DYNAMIC_MACHINE, TYPE_RL78_DYNAMIC_MACHINE)

static void rl78_dynamic_outport_flush_bh(RL78DynamicMachineState *s,
                                          bool force) {
  qemu_mutex_lock(&s->outport_mutex);
  const bool has_enough_data = g_queue_get_length(s->outport_pendings) >= 8;
  GQueue *transmit_data = NULL;

  if (!force && !timer_pending(s->outport_timer)) {
    timer_mod(s->outport_timer,
              qemu_clock_get_ns(QEMU_CLOCK_HOST) + 1000);
  }

  if (has_enough_data || force) {
    timer_del(s->outport_timer);

    transmit_data = s->outport_pendings;
    s->outport_pendings = g_queue_new();
  }
  qemu_mutex_unlock(&s->outport_mutex);

  if (transmit_data) {
    const uint64_t length = g_queue_get_length(transmit_data);
    const uint64_t buffer_size = sizeof(IPCPayloadHeader) + length * sizeof(IPCSignalPayload);
    uint8_t *payload_buffer = g_new(uint8_t, buffer_size);
    IPCPayloadHeader header = {.machine_id = s->machine_id,
                               .payload_count = length};
    IPCSignalPayload *entry = NULL;

    memcpy(payload_buffer, &header, sizeof(IPCPayloadHeader));
    uint8_t *payload_buffer_ptr = payload_buffer + sizeof(IPCPayloadHeader);
    while ((entry = g_queue_pop_head(transmit_data)) != NULL) {
      memcpy(payload_buffer_ptr, entry, sizeof(IPCSignalPayload));
      payload_buffer_ptr += sizeof(IPCSignalPayload);
      g_free(entry);
    }

    qemu_chr_fe_write_all(&s->port_chardev, payload_buffer, buffer_size);
    g_free(payload_buffer);
  }
}

static void rl78_dynamic_outport_timeout_flush(void *opaque) {
  RL78DynamicMachineState *s = opaque;
  rl78_dynamic_outport_flush_bh(s, true);
}

static void rl78_dynamic_outport_try_flush_bh(void *opaque) {
  RL78DynamicMachineState *s = opaque;
  rl78_dynamic_outport_flush_bh(s, false);
}

static void rl78_dynamic_outport_handler(Object *instance, uint64_t index,
                                         const void *payload) {
  RL78DynamicMachineState *s = RL78_DYNAMIC_MACHINE(instance);
  const uint64_t port_id = s->outport_key_table[index];
  const uint64_t sequence = s->outport_seq_table[index];
  s->outport_seq_table[index]++;

  IPCSignalPayload *out = g_new(IPCSignalPayload, 1);
  out->pin_id = port_id;
  out->sequence = sequence;
  out->timestamp = qemu_clock_get_ns(QEMU_CLOCK_HOST);
  out->payload = *(const WirePayload *)payload;

  qemu_mutex_lock(&s->outport_mutex);
  g_queue_push_tail(s->outport_pendings, out);
  qemu_mutex_unlock(&s->outport_mutex);

  qemu_bh_schedule(s->outport_flush_bh);
}

static void rl78_dynamic_append_inport_payload(RL78DynamicMachineState *s, IPCSignalPayload *payload, GList** pending_list) {
    bool inserted = false;
    for(GList* it = *pending_list; it != NULL; it = it->next) {
        const IPCSignalPayload* entry = (IPCSignalPayload*)(it->data);
        if(payload->sequence < entry->sequence) {
            *pending_list = g_list_insert_before(*pending_list, it, payload);
            inserted = true;
            break;
        }
    }

    if(!inserted) {
        *pending_list = g_list_append(*pending_list, payload);
    }
}

static void rl78_dynamic_distribute_payloads(void *opaque) {
  RL78DynamicMachineState *s = opaque;

  qemu_mutex_lock(&s->inport_mutex);
  GList *payloads = s->inport_payloads;
  s->inport_payloads = NULL;
  qemu_mutex_unlock(&s->inport_mutex);

  while(g_list_first(payloads) != NULL) {
    GList* entry = g_list_first(payloads);
    IPCSignalPayload *payload = entry->data;
    payloads = g_list_remove(payloads, payload);

    uint64_t port_index = GPOINTER_TO_UINT(g_hash_table_lookup(
        s->inport_key_table, GUINT_TO_POINTER(payload->pin_id)));

    const uint64_t seq = s->inport_seq_table[port_index];
    if(seq > payload->sequence) {
        // throw away timeout payloads
        continue;
    }

    payload->timestamp = qemu_clock_get_ns(QEMU_CLOCK_HOST);

    IPCSignalPayload *new_payload = g_new0(IPCSignalPayload, 1);
    *new_payload = *payload;
    rl78_dynamic_append_inport_payload(s, new_payload, &s->inport_pending_payloads[port_index]); 
  }
  g_list_free(payloads);

  for(uint64_t port_index = 0; port_index < s->inport_count; port_index++) {
    while(g_list_first(s->inport_pending_payloads[port_index]) != NULL) {
        GList* entry = g_list_first(s->inport_pending_payloads[port_index]);
        IPCSignalPayload* payload = entry->data;
        TransmitPort port = s->inports[port_index];
        const uint64_t current_sequence = s->inport_seq_table[port_index];

        if(payload->sequence > current_sequence && payload->timestamp > qemu_clock_get_ns(QEMU_CLOCK_HOST) - (1000 * 1000)) {
            break;
        }

        s->inport_pending_payloads[port_index] = g_list_remove(s->inport_pending_payloads[port_index], payload);
        transmit_port_payload(&port, &payload->payload);
        s->inport_seq_table[port_index] = payload->sequence + 1;

        g_free(payload);
    }
  }
}

static int rl78_dynamic_port_can_receive(void *opaque) {
  return sizeof(IPCSignalPayload) * 8192;
}

static gint sort_ipc_signal_payload_by_timestamp(gconstpointer a,
                                                 gconstpointer b, gpointer _u) {
  const IPCSignalPayload *pa = a;
  const IPCSignalPayload *pb = b;

  if (pa->timestamp < pb->timestamp) {
    return -1;
  } else if (pa->timestamp > pb->timestamp) {
    return 1;
  }
  return 0;
}

static void rl78_dynamic_port_receive(void *opaque, const uint8_t *buf,
                                      int size) {
  RL78DynamicMachineState *s = opaque;

  g_byte_array_append(s->inport_unhandled_payloads, buf, size);

  const int count =
      s->inport_unhandled_payloads->len / sizeof(IPCSignalPayload);
  GList *payloads = NULL;

  IPCSignalPayload *payload_array = g_new(IPCSignalPayload, count);
  for (int i = 0; i < count; i++) {
    payload_array[i] =
        g_array_index(s->inport_unhandled_payloads, IPCSignalPayload, i);
    payloads = g_list_append(payloads, &payload_array[i]);
  }
  g_byte_array_remove_range(s->inport_unhandled_payloads, 0,
                            count * sizeof(IPCSignalPayload));

  if (likely(count > 0)) {
    payloads = g_list_sort_with_data(
        payloads, sort_ipc_signal_payload_by_timestamp, NULL);

    qemu_mutex_lock(&s->inport_mutex);
    if (s->inport_payloads) {
      s->inport_payloads = g_list_concat(s->inport_payloads, payloads);
    } else {
      s->inport_payloads = payloads;
      aio_bh_schedule_oneshot(qemu_get_aio_context(),
                              rl78_dynamic_distribute_payloads, s);
    }
    qemu_mutex_unlock(&s->inport_mutex);
  }
}

static void send_greetings(RL78DynamicMachineState *s) {
  IPCPayloadHeader header = {.machine_id = s->machine_id, .payload_count = 0};
  qemu_chr_fe_write_all(&s->port_chardev, (const uint8_t *)&header, sizeof(IPCPayloadHeader));
}

static void rl78_dynamic_init(MachineState *machine) {
  RL78DynamicMachineState *s = RL78_DYNAMIC_MACHINE(machine);
  if (!s->config_path) {
    error_report("No config path specified");
    exit(1);
  }

  char *config_json;
  gboolean success =
      g_file_get_contents(s->config_path, &config_json, NULL, NULL);
  if (!success) {
    error_report("Failed to read config file %s", s->config_path);
    exit(1);
  }

  QObject *config = qobject_from_json(config_json, &error_abort);
  g_free(config_json);
  if (!config) {
    error_report("Failed to parse config file %s", s->config_path);
    exit(1);
  }

  MachineStructure *machine_structure;
  Visitor *v = qobject_input_visitor_new(config);
  bool visit_success =
      visit_type_MachineStructure(v, NULL, &machine_structure, &error_abort);
  visit_free(v);
  if (!visit_success) {
    error_report("Failed to construct machine structure");
    exit(1);
  }

  s->machine_id = machine_structure->machine_id;

  ObjectClass *mcuclass = object_class_by_name(machine_structure->mcu);
  if (!mcuclass) {
    error_report("Failed to find MCU class %s", machine_structure->mcu);
    exit(1);
  }
  if (!object_class_dynamic_cast(mcuclass, TYPE_RL78G23_MCU)) {
    error_report("MCU class %s is not a RL78 G23 MCU", machine_structure->mcu);
    exit(1);
  }
  if (object_class_is_abstract(mcuclass)) {
    error_report("MCU class %s is abstract", machine_structure->mcu);
    exit(1);
  }

  object_initialize_child(OBJECT(machine), "mcu", &s->mcu,
                          machine_structure->mcu);
  sysbus_realize(SYS_BUS_DEVICE(&s->mcu), &error_abort);

  Chardev *machine_port = qemu_chr_find("machine-port");
  if (machine_port) {
    qemu_chr_fe_init(&s->port_chardev, machine_port, &error_abort);
    qemu_chr_fe_set_handlers(&s->port_chardev, rl78_dynamic_port_can_receive,
                             rl78_dynamic_port_receive, NULL, NULL, s,
                             iothread_get_g_main_context(s->port_thread), true);

    send_greetings(s);
  } else {
    qemu_log("Failed to find machine-port chardev\n");
  }

  for (MachineCircuitList *it = machine_structure->circuits; it != NULL;
       it = it->next) {
    MachineCircuit *c = it->value;
    ObjectClass *circuit_class = object_class_by_name(c->type);
    if (!circuit_class) {
      error_report("Failed to find circuit class %s", c->type);
      exit(1);
    }
    if (object_class_is_abstract(circuit_class)) {
      error_report("Circuit class %s is abstract", c->type);
      exit(1);
    }

    Object *circuit = object_new(c->type);
    object_property_add_child(OBJECT(machine), c->name, circuit);
    sysbus_realize(SYS_BUS_DEVICE(circuit), &error_abort);
  }

  for (CircuitConnectionList *it = machine_structure->connections; it != NULL;
       it = it->next) {
    CircuitConnection *c = it->value;
    CircuitPin *sink = c->sink_pin;
    CircuitPin *source = c->source_pin;

    Object *sink_circuit = object_property_get_link(
        OBJECT(machine), sink->circuit_name, &error_abort);
    Object *source_circuit = object_property_get_link(
        OBJECT(machine), source->circuit_name, &error_abort);

    connect_port(source_circuit, source->pin_name, source->pin_index,
                 sink_circuit, sink->pin_name, sink->pin_index);
  }

  int outport_count = 0;
  for (MachineOutportList *it = machine_structure->outports; it != NULL;
       it = it->next, outport_count++)
    ;
  receive_port_add(OBJECT(machine), "outport", rl78_dynamic_outport_handler,
                   outport_count);
  s->outport_key_table = g_new(uint64_t, outport_count);
  s->outport_seq_table = g_new(uint64_t, outport_count);

  uint64_t outport_index = 0;
  for (MachineOutportList *it = machine_structure->outports; it != NULL;
       it = it->next, outport_index++) {
    MachineOutport *mo = it->value;
    s->outport_key_table[outport_index] = mo->id;
    s->outport_seq_table[outport_index] = 0;

    for (CircuitPinList *cpit = mo->from; cpit != NULL; cpit = cpit->next) {
      CircuitPin *cp = cpit->value;
      Object *circuit = object_property_get_link(
          OBJECT(machine), cp->circuit_name, &error_abort);
      if (!circuit) {
        error_report("Failed to find circuit %s", cp->circuit_name);
        exit(1);
      }

      connect_port(circuit, cp->pin_name, cp->pin_index, OBJECT(machine),
                   "outport", outport_index);
    }
  }

  int inport_count = 0;
  for (MachineInportList *it = machine_structure->inports; it != NULL;
       it = it->next, inport_count++)
    ;

  TransmitPort *inports = g_new(TransmitPort, inport_count);
  transmit_port_add(OBJECT(machine), "inport", inports, inport_count);
  s->inport_count = inport_count;
  s->inport_key_table = g_hash_table_new(g_direct_hash, g_direct_equal);
  s->inports = inports;
  s->inport_unhandled_payloads = g_byte_array_sized_new(8192);
  s->inport_seq_table = g_new(uint64_t, inport_count);
  s->inport_pending_payloads = g_new(GList*, inport_count);

  uint64_t inport_index = 0;
  for (MachineInportList *it = machine_structure->inports; it != NULL;
       it = it->next, inport_index++) {
    MachineInport *mi = it->value;
    CircuitPinList *cplist = mi->to;
    g_hash_table_insert(s->inport_key_table, GINT_TO_POINTER(mi->id),
                        GINT_TO_POINTER(inport_index));
    s->inport_seq_table[inport_index] = 0;
    s->inport_pending_payloads[inport_index] = NULL;

    for (CircuitPinList *cpit = cplist; cpit != NULL; cpit = cpit->next) {
      CircuitPin *cp = cpit->value;
      Object *circuit = object_property_get_link(
          OBJECT(machine), cp->circuit_name, &error_abort);
      if (!circuit) {
        error_report("Failed to find circuit %s", cp->circuit_name);
        exit(1);
      }

      connect_port(OBJECT(machine), "inport", inport_index, circuit,
                   cp->pin_name, cp->pin_index);
    }
  }

  if (machine->firmware) {
    if (!rl78_load_firmware(machine->firmware)) {
      error_report("Failed to load firmware image %s", machine->firmware);
      exit(1);
    }
  }
}

static void set_config_path(Object *o, const char *value, Error **errp) {
  RL78DynamicMachineState *s = RL78_DYNAMIC_MACHINE(o);
  if (s->config_path) {
    g_free(s->config_path);
  }

  s->config_path = g_strdup(value);
}

static void rl78_dynamic_instance_init(Object *obj) {
  RL78DynamicMachineState *s = RL78_DYNAMIC_MACHINE(obj);

  s->config_path = NULL;

  s->port_thread = iothread_create("outport-thread", &error_abort);
  s->outport_timer =
      aio_timer_new(iothread_get_aio_context(s->port_thread), QEMU_CLOCK_HOST,
                    SCALE_NS, rl78_dynamic_outport_timeout_flush, s);
  s->outport_pendings = g_queue_new();
  s->outport_key_table = NULL;
  s->outport_flush_bh = aio_bh_new(iothread_get_aio_context(s->port_thread),
                                   rl78_dynamic_outport_try_flush_bh, s);

  qemu_mutex_init(&s->outport_mutex);
  qemu_mutex_init(&s->inport_mutex);
}

static void rl78_dynamic_class_init(ObjectClass *oc, const void *data) {
  MachineClass *mc = MACHINE_CLASS(oc);

  mc->init = rl78_dynamic_init;
  mc->default_cpus = 1;
  mc->min_cpus = mc->default_cpus;
  mc->max_cpus = mc->default_cpus;
  mc->no_floppy = 1;
  mc->no_parallel = 1;
  mc->no_cdrom = 1;

  object_class_property_add_str(oc, "config-path", NULL, set_config_path);
  object_class_property_set_description(
      oc, "config-path", "Path to the machine structure configuration file");
}

static const TypeInfo rl78_master_machine_types[] = {{
    .name = TYPE_RL78_DYNAMIC_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(RL78DynamicMachineState),
    .instance_init = rl78_dynamic_instance_init,
    .class_size = sizeof(RL78DynamicMachineClass),
    .class_init = rl78_dynamic_class_init,
}};

DEFINE_TYPES(rl78_master_machine_types)
