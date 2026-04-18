#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/bitops.h"
#include "hw/core/irq.h"
#include "hw/core/resettable.h"
#include "gpio.h"

struct RL78GPIOClass {
    /* private */
    SysBusDeviceClass parent_class;

    /* public */
    ResettablePhases parent_phases;
};
typedef struct RL78GPIOClass RL78GPIOClass;

DECLARE_CLASS_CHECKERS(RL78GPIOClass, RL78_GPIO, TYPE_RL78_GPIO)

static void rl78_gpio_write_mode(void *opaque, hwaddr offset, uint64_t data,
                                 unsigned size)
{
    RL78GPIOState *s = RL78_GPIO(opaque);

    assert(offset < RL78_GPIO_REG_NUM);

    if (offset == 13) {
        qemu_log_mask(LOG_GUEST_ERROR, "PM13 register is not exist");
        return;
    }

    switch (offset) {
    case 10:
        if (!(data & 0x80)) {
            qemu_log_mask(LOG_GUEST_ERROR, "PM10.bit7 must be 1");
        }

        s->mode[offset] = data | 0x80;
        break;
    case 12:
        if (!(data & 0x18)) {
            qemu_log_mask(LOG_GUEST_ERROR, "PM12.bit4 and PM12.bit3 must be 1");
        }

        s->mode[offset] = data | 0x18;
        break;
    case 15:
        if (!(data & 0x80)) {
            qemu_log_mask(LOG_GUEST_ERROR, "PM15.bit7 must be 1");
        }

        s->mode[offset] = data | 0x80;
        break;
    default:
        s->mode[offset] = data;
        break;
    }
}

static void rl78_gpio_write_level(void *opaque, hwaddr offset, uint64_t data,
                                  unsigned size)
{
    RL78GPIOState *s = RL78_GPIO(opaque);

    assert(offset < RL78_GPIO_REG_NUM);

    switch (offset) {
    case 10:
        if (data & 0x80) {
            qemu_log_mask(LOG_GUEST_ERROR, "PM10.bit7 must be 0");
        }
        data &= ~0x80;
        break;
    case 13:
        if (data & 0x7E) {
            qemu_log_mask(LOG_GUEST_ERROR, "PM13.bit1-PM13.bit6 must be 0");
        }

        data &= ~0x7E;
        break;
    case 15:
        if (data & 0x80) {
            qemu_log_mask(LOG_GUEST_ERROR, "PM15.bit7 must be 0");
        }

        data &= ~0x80;
        break;
    }

    // only output port levels are changed.
    const uint8_t old_level = s->level[offset];
    s->level[offset] &= s->mode[offset];
    s->level[offset] |= ~s->mode[offset] & data;

    for(int i = 0; i < 8; i++) {
        const uint8_t old_bit = extract32(old_level, i, 1);
        const uint8_t new_bit = extract32(s->level[offset], i, 1);
        const uint8_t gpio_index = offset * 8 + i;

        if(old_bit != new_bit) {
            qemu_set_irq(s->outputs[gpio_index], new_bit);
        }
    }
}

static void rl78_gpio_write_redirection(void *opaque, hwaddr offset,
                                        uint64_t data, unsigned size)
{
    RL78GPIOState *s = RL78_GPIO(opaque);

    qemu_log_mask(LOG_UNIMP, "not implemented: PIOR register");

    if (data & 0xC0) {
        qemu_log_mask(LOG_GUEST_ERROR, "PIOR.bit6 and PIOR.bit7 must be 0");
        data &= ~0xC0;
    }

    s->redirection = data;
}

static void rl78_gpio_write_global_input_disable(void *opaque, hwaddr offset,
                                                 uint64_t data, unsigned size)
{
    RL78GPIOState *s = RL78_GPIO(opaque);
    qemu_log_mask(LOG_UNIMP, "not implemented: PIDIS register");

    if (data & 0xFE) {
        qemu_log_mask(LOG_GUEST_ERROR, "PIDIS.bit1-7 must be 0");
        data &= ~0xFE;
    }

    s->global_input_disable = data;
}

static void rl78_gpio_write_pullup(RL78GPIOState *s, hwaddr offset,
                                   uint64_t data)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: PU register");

    const uint8_t index = offset & 0x0F;

    s->pullup[index] = data;
}

static void rl78_gpio_write_input_mode(RL78GPIOState *s, hwaddr offset,
                                       uint64_t data)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: PIM register");

    const uint8_t index = offset & 0x0F;

    s->input_mode[index] = data;
}

static void rl78_gpio_write_output_mode(RL78GPIOState *s, hwaddr offset,
                                        uint64_t data)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: POM register");

    const uint8_t index = offset & 0x0F;

    s->output_mode[index] = data;
}

static void rl78_gpio_write_control_a(RL78GPIOState *s, hwaddr offset,
                                      uint64_t data)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: PMCA register");

    const uint8_t index = offset & 0x0F;

    s->control_a[index] = data;
}

static void rl78_gpio_write_other0(void *opaque, hwaddr offset, uint64_t data,
                                   unsigned size)
{
    RL78GPIOState *s           = RL78_GPIO(opaque);
    const uint8_t group_offset = (offset >> 4) & 0x0F;

    switch (group_offset) {
    case 0x00:
        rl78_gpio_write_pullup(s, offset, data);
        break;
    case 0x01:
        rl78_gpio_write_input_mode(s, offset, data);
        break;
    case 0x02:
        rl78_gpio_write_output_mode(s, offset, data);
        break;
    case 0x03:
        rl78_gpio_write_control_a(s, offset, data);
        break;
    default:
        g_assert_not_reached();
    }
}

static void rl78_gpio_write_control_t(RL78GPIOState *s, hwaddr offset,
                                      uint64_t data)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: PMCT register");

    const uint8_t index = offset & 0x0F;
    s->control_t[index] = data;
}

static void rl78_gpio_write_control_e(RL78GPIOState *s, hwaddr offset,
                                      uint64_t data)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: PMCE register");

    const uint8_t index = offset & 0x0F;
    s->control_e[index] = data;
}

static void rl78_gpio_write_digital_input_disable(RL78GPIOState *s,
                                                  hwaddr offset, uint64_t data)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: PDIDIS register");

    const uint8_t index             = offset & 0x0F;
    s->digital_input_disable[index] = data;
}

static void rl78_gpio_write_ampere_selection(RL78GPIOState *s, hwaddr offset,
                                             uint64_t data)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: CCS register");

    const uint8_t index = offset & 0x0F;
    if (data > RL78_GPIO_AMPERE_SELECTION_15MA) {
        qemu_log_mask(LOG_GUEST_ERROR, "CCS value is invalid: %ld", data);
        data = RL78_GPIO_AMPERE_SELECTION_HIZ;
    }

    s->ampere_selections[index] = data;
}

static void rl78_gpio_write_control_current(RL78GPIOState *s, hwaddr offset,
                                            uint64_t data)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: CCDE register");
    s->control_current = data;
}

static void rl78_gpio_write_dc_level(RL78GPIOState *s, hwaddr offset,
                                     uint64_t data)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: PTDC register");

    if (data & 0x80) {
        qemu_log_mask(LOG_GUEST_ERROR, "PTDC.bit7 must be 0");
        data &= ~0x80;
    }

    s->dc_level = data;
}

static void rl78_gpio_write_function_output(RL78GPIOState *s, hwaddr offset,
                                            uint64_t data)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: PFOE register");

    switch (offset) {
    case 0:
        s->function_output &= 0xFF00;
        s->function_output |= data & 0x00FF;
        break;
    case 1:
        if (!((data & 0x80) && (data & 0x40))) {
            qemu_log_mask(LOG_GUEST_ERROR, "PFOE.bit6 and PFOE.bit7 must be 1");
            data |= 0xC0;
        }

        s->function_output &= 0x00FF;
        s->function_output |= (data & 0x00FF) << 8;
        break;
    default:
        g_assert_not_reached();
    }
}

static void rl78_gpio_write_other1(void *opaque, hwaddr offset, uint64_t data,
                                   unsigned size)
{
    RL78GPIOState *s           = RL78_GPIO(opaque);
    const uint8_t group_offset = (offset >> 4) & 0x0F;

    switch (group_offset) {
    case 0x00:
        rl78_gpio_write_control_t(s, offset, data);
        break;
    case 0x02:
        rl78_gpio_write_control_e(s, offset, data);
        break;
    case 0x05:
        rl78_gpio_write_digital_input_disable(s, offset, data);
        break;
    case 0x04: {
        const uint8_t index = offset & 0x0F;
        switch (index) {
        case 0x00:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
            rl78_gpio_write_ampere_selection(s, offset, data);
            break;
        case 0x08:
            rl78_gpio_write_control_current(s, offset, data);
            break;
        case 0x09:
            rl78_gpio_write_dc_level(s, offset, data);
            break;
        case 0x0A:
            rl78_gpio_write_function_output(s, 0, data);
            break;
        case 0x0B:
            rl78_gpio_write_function_output(s, 1, data);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "unknown register: offset = %ld\n",
                          offset);
            break;
        }
        break;
    }
    }
}
static uint64_t rl78_gpio_read_mode(void *opaque, hwaddr offset, unsigned size)
{
    RL78GPIOState *s = RL78_GPIO(opaque);

    assert(offset < RL78_GPIO_REG_NUM);

    if (offset == 13) {
        qemu_log_mask(LOG_GUEST_ERROR, "PM13 register is not exist");
        return 0;
    }

    return s->mode[offset];
}

static uint64_t rl78_gpio_read_level(void *opaque, hwaddr offset, unsigned size)
{
    RL78GPIOState *s = RL78_GPIO(opaque);

    assert(offset < RL78_GPIO_REG_NUM);

    return s->level[offset];
}

static uint64_t rl78_gpio_read_redirection(void *opaque, hwaddr offset,
                                           unsigned size)
{
    RL78GPIOState *s = RL78_GPIO(opaque);

    qemu_log_mask(LOG_UNIMP, "not implemented: PIOR register");

    return s->redirection;
}

static uint64_t rl78_gpio_read_global_input_disable(void *opaque, hwaddr offset,
                                                    unsigned size)
{
    RL78GPIOState *s = RL78_GPIO(opaque);

    qemu_log_mask(LOG_UNIMP, "not implemented: PIDIS register");

    return s->global_input_disable;
}

static uint64_t rl78_gpio_read_pullup(RL78GPIOState *s, hwaddr offset)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: PU register");

    const uint8_t index = offset & 0x0F;

    return s->pullup[index];
}

static uint64_t rl78_gpio_read_input_mode(RL78GPIOState *s, hwaddr offset)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: PIM register");

    const uint8_t index = offset & 0x0F;

    return s->input_mode[index];
}

static uint64_t rl78_gpio_read_output_mode(RL78GPIOState *s, hwaddr offset)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: POM register");

    const uint8_t index = offset & 0x0F;

    return s->output_mode[index];
}

static uint64_t rl78_gpio_read_control_a(RL78GPIOState *s, hwaddr offset)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: PMCA register");

    const uint8_t index = offset & 0x0F;

    return s->control_a[index];
}

static uint64_t rl78_gpio_read_other0(void *opaque, hwaddr offset,
                                      unsigned size)
{
    RL78GPIOState *s           = RL78_GPIO(opaque);
    const uint8_t group_offset = (offset >> 4) & 0x0F;

    switch (group_offset) {
    case 0x00:
        return rl78_gpio_read_pullup(s, offset);
    case 0x01:
        return rl78_gpio_read_input_mode(s, offset);
    case 0x02:
        return rl78_gpio_read_output_mode(s, offset);
    case 0x03:
        return rl78_gpio_read_control_a(s, offset);
    default:
        g_assert_not_reached();
    }
}

static uint64_t rl78_gpio_read_control_t(RL78GPIOState *s, hwaddr offset)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: PMCT register");

    const uint8_t index = offset & 0x0F;
    return s->control_t[index];
}

static uint64_t rl78_gpio_read_control_e(RL78GPIOState *s, hwaddr offset)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: PMCE register");

    const uint8_t index = offset & 0x0F;
    return s->control_e[index];
}

static uint64_t rl78_gpio_read_digital_input_disable(RL78GPIOState *s,
                                                     hwaddr offset)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: PDIDIS register");

    const uint8_t index = offset & 0x0F;
    return s->digital_input_disable[index];
}

static uint64_t rl78_gpio_read_ampere_selection(RL78GPIOState *s, hwaddr offset)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: CCS register");

    const uint8_t index = offset & 0x0F;
    return s->ampere_selections[index];
}

static uint64_t rl78_gpio_read_control_current(RL78GPIOState *s, hwaddr offset)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: CCDE register");
    return s->control_current;
}

static uint64_t rl78_gpio_read_dc_level(RL78GPIOState *s, hwaddr offset)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: PTDC register");
    return s->dc_level;
}

static uint64_t rl78_gpio_read_function_output(RL78GPIOState *s, hwaddr offset)
{
    qemu_log_mask(LOG_UNIMP, "not implemented: PFOE register");

    switch (offset) {
    case 0:
        return extract32(s->function_output, 0, 8);
    case 1:
        return extract32(s->function_output, 8, 8);
    default:
        g_assert_not_reached();
    }
}

static uint64_t rl78_gpio_read_other1(void *opaque, hwaddr offset,
                                      unsigned size)
{
    RL78GPIOState *s           = RL78_GPIO(opaque);
    const uint8_t group_offset = (offset >> 4) & 0x0F;

    switch (group_offset) {
    case 0x00:
        return rl78_gpio_read_control_t(s, offset);
        break;
    case 0x02:
        return rl78_gpio_read_control_e(s, offset);
        break;
    case 0x05:
        return rl78_gpio_read_digital_input_disable(s, offset);
    case 0x04: {
        const uint8_t index = offset & 0x0F;
        switch (index) {
        case 0x00:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
            return rl78_gpio_read_ampere_selection(s, offset);
        case 0x08:
            return rl78_gpio_read_control_current(s, offset);
        case 0x09:
            return rl78_gpio_read_dc_level(s, offset);
        case 0x0A:
            return rl78_gpio_read_function_output(s, 0);
        case 0x0B:
            return rl78_gpio_read_function_output(s, 1);
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "unknown register: offset = %ld\n", offset);
            return 0;
        }
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "unknown register: offset = %ld\n", offset);
        return 0;
    }
}

static const MemoryRegionOps rl78_gpio_ops_mode = {
    .read = rl78_gpio_read_mode,
    .write = rl78_gpio_write_mode,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
};

static const MemoryRegionOps rl78_gpio_ops_level = {
    .read = rl78_gpio_read_level,
    .write = rl78_gpio_write_level,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
};

static const MemoryRegionOps rl78_gpio_ops_redirection = {
    .read = rl78_gpio_read_redirection,
    .write = rl78_gpio_write_redirection,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
};

static const MemoryRegionOps rl78_gpio_global_input_disable = {
    .read = rl78_gpio_read_global_input_disable,
    .write = rl78_gpio_write_global_input_disable,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
};

static const MemoryRegionOps rl78_gpio_other0 = {
    .read = rl78_gpio_read_other0,
    .write = rl78_gpio_write_other0,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
};

static const MemoryRegionOps rl78_gpio_other1 = {
    .read = rl78_gpio_read_other1,
    .write = rl78_gpio_write_other1,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
};

static void rl78_gpio_recv_inputs(void *opaque, int irq, int level) {
    RL78GPIOState *s = RL78_GPIO(opaque);

    const uint8_t index = (irq >> 4);
    const uint8_t shamt = irq & 0x0F;
    const uint8_t bit = !!level;

    if(extract32(s->mode[index], shamt, 1) == RL78_GPIO_MODE_TYPE_INPUT) {
        s->level[index] &= ~(1 << shamt);
        s->level[index] |= bit << shamt;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "input signal is received, but P%d is not assigned as input by PM register", irq);
    }
}

static void rl78_gpio_init(Object *obj) {
    RL78GPIOState *s = RL78_GPIO(obj);

    memory_region_init_io(&s->mmio[0], obj, &rl78_gpio_ops_mode, s,
                          "mode", 16);
    memory_region_init_io(&s->mmio[1], obj, &rl78_gpio_ops_level, s,
                          "level", 16);
    memory_region_init_io(&s->mmio[2], obj, &rl78_gpio_ops_redirection, s,
                          "redirection", 1);
    memory_region_init_io(&s->mmio[3], obj, &rl78_gpio_global_input_disable, s,
                          "global_input_disable", 1);
    memory_region_init_io(&s->mmio[4], obj, &rl78_gpio_other0, s, "other0", 0x40);
    memory_region_init_io(&s->mmio[5], obj, &rl78_gpio_other1, s, "other1", 0x60);

    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio[0]);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio[1]);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio[2]);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio[3]);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio[4]);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio[5]);

    qdev_init_gpio_out_named(DEVICE(obj), s->outputs, "gpio-out", RL78_GPIO_PIN_NUM);
    qdev_init_gpio_in_named(DEVICE(obj), rl78_gpio_recv_inputs, "gpio-in", RL78_GPIO_PIN_NUM);
}

static void rl78_gpio_reset_hold(Object *obj, ResetType type)
{
    RL78GPIOState *s = RL78_GPIO(obj);

    for (int i = 0; i < RL78_GPIO_REG_NUM; i++) {
        s->mode[i] = 0xFF;
        s->level[i] = 0x00;
        s->pullup[i] = 0x00;
        s->input_mode[i] = 0x00;
        s->output_mode[i] = 0x00;
        s->digital_input_disable[i] = 0x00; 
        s->control_a[i] = 0xFF;    
        s->control_t[i] = 0x00;
        s->control_e[i] = 0x00;
    }

    for(int i = 0; i < ARRAY_SIZE(s->ampere_selections); i++) {
        s->ampere_selections[i] = RL78_GPIO_AMPERE_SELECTION_HIZ;
    }

    s->redirection = 0x00;
    s->global_input_disable = 0x00;
    s->control_current = 0x00;
    s->dc_level = 0x00;
    s->function_output = 0x0000;
    
    s->pullup[4] = 0x01;
}

static void rl78_gpio_class_init(ObjectClass *klass, const void *data)
{
    RL78GPIOClass *gc = RL78_GPIO_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    resettable_class_set_parent_phases(rc, NULL, rl78_gpio_reset_hold, NULL,
                                       &gc->parent_phases);
}

static const TypeInfo rl78_gpio_info = {
    .name = TYPE_RL78_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init = rl78_gpio_init,
    .instance_size = sizeof(RL78GPIOState),
    .class_init = rl78_gpio_class_init,
    .class_size = sizeof(RL78GPIOClass),
};

static void rl78_gpio_register_types(void)
{
    type_register_static(&rl78_gpio_info);
}

type_init(rl78_gpio_register_types)
