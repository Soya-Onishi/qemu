#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/bitops.h"
#include "hw/core/sysbus.h"
#include "hw/core/registerfields.h"
#include "hw/core/resettable.h"
#include "irq.h"

struct RL78IRQControllerClass {
    /*< private >*/
    SysBusDeviceClass parent_class;

    /*< public >*/
    ResettablePhases parent_phases;
};
typedef struct RL78IRQControllerClass RL78IRQControllerClass;

DECLARE_CLASS_CHECKERS(RL78IRQControllerClass, RL78_IRQ_CONTROLLER,
                       TYPE_RL78_IRQ_CONTROLLER)

// From 0xFFFE0
REG16(IF0, 0x00)
REG8(IF0L, 0x00)
REG8(IF0H, 0x01)

REG16(IF1, 0x02)
REG8(IF1L, 0x02)
REG8(IF1H, 0x03)

REG16(MK0, 0x04)
REG8(MK0L, 0x04)
REG8(MK0H, 0x05)

REG16(MK1, 0x06)
REG8(MK1L, 0x06)
REG8(MK1H, 0x07)

REG16(PR00, 0x08)
REG8(PR00L, 0x08)
REG8(PR00H, 0x09)

REG16(PR01, 0x0A)
REG8(PR01L, 0x0A)
REG8(PR01H, 0x0B)

REG16(PR10, 0x0C)
REG8(PR10L, 0x0C)
REG8(PR10H, 0x0D)

REG16(PR11, 0x0E)
REG8(PR11L, 0x0E)
REG8(PR11H, 0x0F)

// From 0xFFFD0
REG16(IF2, 0x00)
REG8(IF2L, 0x00)
REG8(IF2H, 0x01)

REG16(IF3, 0x02)
REG8(IF3L, 0x02)
REG8(IF3H, 0x03)

REG16(MK2, 0x04)
REG8(MK2L, 0x04)
REG8(MK2H, 0x05)

REG16(MK3, 0x06)
REG8(MK3L, 0x06)
REG8(MK3H, 0x07)

REG16(PR02, 0x08)
REG8(PR02L, 0x08)
REG8(PR02H, 0x09)

REG16(PR03, 0x0A)
REG8(PR03L, 0x0A)
REG8(PR03H, 0x0B)

REG16(PR12, 0x0C)
REG8(PR12L, 0x0C)
REG8(PR12H, 0x0D)

REG16(PR13, 0x0E)
REG8(PR13L, 0x0E)
REG8(PR13H, 0x0F)

// From 0xFFF38
REG8(EGP0, 0x00)
REG8(EGN0, 0x01)
REG8(EGP1, 0x02)
REG8(EGN1, 0x03)

static void rl78_irq_update_priority_low(RL78IRQControllerState *s,
                                         uint8_t head_index, uint16_t value);
static void rl78_irq_update_priority_high(RL78IRQControllerState *s,
                                          uint8_t head_index, uint16_t value);

static uint16_t rl78_irq_read_iflag(RL78IRQControllerState *s,
                                    uint8_t head_offset);
static uint16_t rl78_irq_read_mask(RL78IRQControllerState *s,
                                   uint8_t head_offset);
static uint16_t rl78_irq_read_priority_low(RL78IRQControllerState *s,
                                           uint8_t head_index);
static uint16_t rl78_irq_read_priority_high(RL78IRQControllerState *s,
                                            uint8_t head_index);
static uint8_t rl78_irq_read_egp(RL78IRQControllerState *s, uint8_t head_index);
static uint8_t rl78_irq_read_egn(RL78IRQControllerState *s, uint8_t head_index);

int rl78_irq_pack_irqlevel(uint8_t index, uint8_t priority, uint8_t enable)
{
    assert(sizeof(int) >= sizeof(uint32_t));

    return ((uint32_t)enable << 31) | ((uint32_t)priority << 8) |
           (uint32_t)index;
}

void rl78_irq_unpack_irqlevel(int level, uint8_t *irq_index, uint8_t *priority,
                              uint8_t *enable)
{
    assert(sizeof(int) >= sizeof(uint32_t));

    *irq_index = level & 0xFF;
    *priority  = (level >> 8) & 0x03;
    *enable    = (level >> 31) & 0x01;
}

static void rl78_irq_set_irq(RL78IRQControllerState *s)
{
    for (int priority = 0; priority < RL78_IRQ_PRIORITY_NUM; priority++) {
        uint64_t priority_mask = 0;
        for (int i = 0; i < RL78_CPU_IRQ_NUM; i++) {
            uint64_t mask  = s->irq_priority[i] == priority ? 1 : 0;
            priority_mask |= mask << i;
        }

        const uint64_t mask     = ~s->irq_mask;
        const uint64_t irq_reqs = s->irq_flag & mask & priority_mask;
        if (irq_reqs) {
            const uint8_t irq_index = ctz64(irq_reqs);
            const int irqlevel = rl78_irq_pack_irqlevel(irq_index, priority, 1);
            qemu_set_irq(s->irq_req, irqlevel);
            return;
        } else {
            qemu_set_irq(s->irq_req, 0);
        }
    }

    qemu_set_irq(s->irq_req, 0);
}

static void rl78_irq_update_iflag_lo_byte(RL78IRQControllerState *s,
                                          uint8_t offset, uint8_t value)
{
    const uint8_t bit_offset = offset * 16;

    s->irq_flag = deposit64(s->irq_flag, bit_offset, 8, value);

    rl78_irq_set_irq(s);
}

static void rl78_irq_update_iflag_hi_byte(RL78IRQControllerState *s,
                                          uint8_t offset, uint8_t value)
{
    const uint8_t bit_offset = offset * 16 + 8;

    s->irq_flag = deposit64(s->irq_flag, bit_offset, 8, value);

    rl78_irq_set_irq(s);
}

static void rl78_irq_update_iflag_word(RL78IRQControllerState *s,
                                       uint8_t head_offset, uint16_t value)
{
    const uint8_t bit_offset = head_offset * 16;

    s->irq_flag = deposit64(s->irq_flag, bit_offset, 16, value);

    rl78_irq_set_irq(s);
}

static void rl78_irq_update_mask_lo_byte(RL78IRQControllerState *s,
                                         uint8_t offset, uint8_t value)
{
    const uint8_t bit_offset = offset * 16;

    s->irq_mask = deposit64(s->irq_mask, bit_offset, 8, value);

    rl78_irq_set_irq(s);
}

static void rl78_irq_update_mask_hi_byte(RL78IRQControllerState *s,
                                         uint8_t offset, uint8_t value)
{
    const uint8_t bit_offset = offset * 16 + 8;

    s->irq_mask = deposit64(s->irq_mask, bit_offset, 8, value);

    rl78_irq_set_irq(s);
}

static void rl78_irq_update_mask_word(RL78IRQControllerState *s,
                                      uint8_t head_offset, uint16_t value)
{
    const uint8_t bit_offset = head_offset * 16;

    s->irq_mask = deposit64(s->irq_mask, bit_offset, 16, value);

    rl78_irq_set_irq(s);
}

static void rl78_irq_update_priority_low_lo_byte(RL78IRQControllerState *s,
                                                 uint8_t offset, uint8_t value)
{
    uint16_t priority = rl78_irq_read_priority_low(s, offset);

    priority = deposit32(priority, 0, 8, value);

    rl78_irq_update_priority_low(s, offset, priority);
}

static void rl78_irq_update_priority_low_hi_byte(RL78IRQControllerState *s,
                                                 uint8_t offset, uint8_t value)
{
    uint16_t priority = rl78_irq_read_priority_low(s, offset);

    priority = deposit32(priority, 8, 8, value);

    rl78_irq_update_priority_low(s, offset, priority);
}

static void rl78_irq_update_priority_low(RL78IRQControllerState *s,
                                         uint8_t head_index, uint16_t value)
{
    for (int i = 0; i < sizeof(value); i++) {
        const uint8_t index = (head_index * 16) + i;
        const uint8_t bit   = (value >> i) & 0x01;

        s->irq_priority[index] = deposit32(s->irq_priority[index], 0, 1, bit);
    }

    rl78_irq_set_irq(s);
}

static void rl78_irq_update_priority_high_lo_byte(RL78IRQControllerState *s,
                                                  uint8_t offset, uint8_t value)
{
    uint16_t priority = rl78_irq_read_priority_high(s, offset);

    priority = deposit32(priority, 0, 8, value);

    rl78_irq_update_priority_high(s, offset, priority);
}

static void rl78_irq_update_priority_high_hi_byte(RL78IRQControllerState *s,
                                                  uint8_t offset, uint8_t value)
{
    uint16_t priority = rl78_irq_read_priority_high(s, offset);

    priority = deposit32(priority, 8, 8, value);

    rl78_irq_update_priority_high(s, offset, priority);
}

static void rl78_irq_update_priority_high(RL78IRQControllerState *s,
                                          uint8_t head_index, uint16_t value)
{
    for (int i = 0; i < sizeof(value); i++) {
        const uint8_t index = (head_index * 16) + i;
        const uint8_t bit   = (value >> i) & 0x01;

        s->irq_priority[index] = deposit32(s->irq_priority[index], 1, 1, bit);
    }

    rl78_irq_set_irq(s);
}

#define RL78_IRQ_EGP_MASK (0x02)
#define RL78_IRQ_EGN_MASK (0x01)

static void rl78_irq_update_egp(RL78IRQControllerState *s, uint8_t head_index,
                                uint8_t value)
{
    for (int i = 0; i < sizeof(value); i++) {
        const uint8_t index = (head_index * 8) + i;
        const uint8_t bit   = (value >> i) & 0x01;

        s->edge_types[index] &= RL78_IRQ_EGN_MASK;
        s->edge_types[index] |= (bit << 1);
    }
}

static void rl78_irq_update_egn(RL78IRQControllerState *s, uint8_t head_index,
                                uint8_t value)
{
    for (int i = 0; i < sizeof(value); i++) {
        const uint8_t index = (head_index * 8) + i;
        const uint8_t bit   = (value >> i) & 0x01;

        s->edge_types[index] &= RL78_IRQ_EGP_MASK;
        s->edge_types[index] |= bit;
    }
}

static uint16_t rl78_irq_read_iflag(RL78IRQControllerState *s,
                                    uint8_t head_offset)
{
    const uint8_t bit_offset = head_offset * 8;
    return (s->irq_flag >> bit_offset) & 0xFFFF;
}

static uint16_t rl78_irq_read_mask(RL78IRQControllerState *s,
                                   uint8_t head_offset)
{
    const uint8_t bit_offset = head_offset * 8;
    return (s->irq_mask >> bit_offset) & 0xFFFF;
}

static uint16_t rl78_irq_read_priority_low(RL78IRQControllerState *s,
                                           uint8_t head_index)
{
    uint16_t bits = 0;

    for (int i = 0; i < sizeof(bits); i++) {
        const uint8_t index = (head_index * 8) + i;
        const uint8_t bit   = s->irq_priority[index] & 0x01 ? 1 : 0;

        bits |= bit << i;
    }

    return bits;
}

static uint16_t rl78_irq_read_priority_high(RL78IRQControllerState *s,
                                            uint8_t head_index)
{
    uint16_t bits = 0;
    for (int i = 0; i < sizeof(bits); i++) {
        const uint8_t index = (head_index * 8) + i;
        const uint8_t bit   = s->irq_priority[index] & 0x02 ? 1 : 0;

        bits |= bit << i;
    }
    return bits;
}

static uint8_t rl78_irq_read_egp(RL78IRQControllerState *s, uint8_t head_index)
{
    uint8_t bits = 0;
    for (int i = 0; i < sizeof(bits); i++) {
        const uint8_t index = (head_index * 8) + i;
        const uint8_t bit   = s->edge_types[index] & RL78_IRQ_EGP_MASK ? 1 : 0;

        bits |= bit << i;
    }

    return bits;
}

static uint8_t rl78_irq_read_egn(RL78IRQControllerState *s, uint8_t head_index)
{
    uint8_t bits = 0;
    for (int i = 0; i < sizeof(bits); i++) {
        const uint8_t index = (head_index * 8) + i;
        const uint8_t bit   = s->edge_types[index] & RL78_IRQ_EGN_MASK ? 1 : 0;

        bits |= bit << i;
    }
    return bits;
}

static void rl78_irq_write0_byte(RL78IRQControllerState *s, uint8_t offset,
                                 uint8_t data)
{
    switch (offset) {
    case A_IF0L:
        rl78_irq_update_iflag_lo_byte(s, 0, data);
        break;
    case A_IF0H:
        rl78_irq_update_iflag_hi_byte(s, 0, data);
        break;
    case A_IF1L:
        rl78_irq_update_iflag_lo_byte(s, 1, data);
        break;
    case A_IF1H:
        rl78_irq_update_iflag_hi_byte(s, 1, data);
        break;
    case A_MK0L:
        rl78_irq_update_mask_lo_byte(s, 0, data);
        break;
    case A_MK0H:
        rl78_irq_update_mask_hi_byte(s, 0, data);
        break;
    case A_MK1L:
        rl78_irq_update_mask_lo_byte(s, 1, data);
        break;
    case A_MK1H:
        rl78_irq_update_mask_hi_byte(s, 1, data);
        break;
    case A_PR00L:
        rl78_irq_update_priority_low_lo_byte(s, 0, data);
        break;
    case A_PR00H:
        rl78_irq_update_priority_low_hi_byte(s, 0, data);
        break;
    case A_PR01L:
        rl78_irq_update_priority_low_lo_byte(s, 1, data);
        break;
    case A_PR01H:
        rl78_irq_update_priority_low_hi_byte(s, 1, data);
        break;
    case A_PR10L:
        rl78_irq_update_priority_high_lo_byte(s, 0, data);
        break;
    case A_PR10H:
        rl78_irq_update_priority_high_hi_byte(s, 0, data);
        break;
    case A_PR11L:
        rl78_irq_update_priority_high_lo_byte(s, 1, data);
        break;
    case A_PR11H:
        rl78_irq_update_priority_high_hi_byte(s, 1, data);
        break;
    default:
        g_assert_not_reached();
        break;
    }
}

static void rl78_irq_write0_word(RL78IRQControllerState *s, uint8_t offset,
                                 uint16_t data)
{
    switch (offset) {
    case A_IF0:
        rl78_irq_update_iflag_word(s, 0, data);
        break;
    case A_IF1:
        rl78_irq_update_iflag_word(s, 1, data);
        break;
    case A_MK0:
        rl78_irq_update_mask_word(s, 0, data);
        break;
    case A_MK1:
        rl78_irq_update_mask_word(s, 1, data);
        break;
    case A_PR00:
        rl78_irq_update_priority_low(s, 0, data);
        break;
    case A_PR01:
        rl78_irq_update_priority_low(s, 1, data);
        break;
    case A_PR10:
        rl78_irq_update_priority_high(s, 0, data);
        break;
    case A_PR11:
        rl78_irq_update_priority_high(s, 1, data);
        break;
    default:
        g_assert_not_reached();
        break;
    }
}

static void rl78_irq_write0(void *opaque, hwaddr offset, uint64_t data,
                            unsigned size)
{
    RL78IRQControllerState *s = RL78_IRQ_CONTROLLER(opaque);

    switch (size) {
    case 1:
        rl78_irq_write0_byte(s, offset, data);
        break;
    case 2:
        rl78_irq_write0_word(s, offset, data);
        break;
    default:
        g_assert_not_reached();
        break;
    }
}

static uint16_t rl78_irq_read0_byte(RL78IRQControllerState *s, uint8_t offset)
{
    switch (offset) {
    case A_IF0L:
        return extract16(rl78_irq_read_iflag(s, 0), 0, 8);
    case A_IF0H:
        return extract16(rl78_irq_read_iflag(s, 0), 8, 8);
    case A_IF1L:
        return extract16(rl78_irq_read_iflag(s, 1), 0, 8);
    case A_IF1H:
        return extract16(rl78_irq_read_iflag(s, 1), 8, 8);
    case A_MK0L:
        return extract16(rl78_irq_read_mask(s, 0), 0, 8);
    case A_MK0H:
        return extract16(rl78_irq_read_mask(s, 0), 8, 8);
    case A_MK1L:
        return extract16(rl78_irq_read_mask(s, 1), 0, 8);
    case A_MK1H:
        return extract16(rl78_irq_read_mask(s, 1), 8, 8);
    case A_PR00L:
        return extract16(rl78_irq_read_priority_low(s, 0), 0, 8);
    case A_PR00H:
        return extract16(rl78_irq_read_priority_low(s, 0), 8, 8);
    case A_PR01L:
        return extract16(rl78_irq_read_priority_low(s, 1), 0, 8);
    case A_PR01H:
        return extract16(rl78_irq_read_priority_low(s, 1), 8, 8);
    case A_PR10L:
        return extract16(rl78_irq_read_priority_high(s, 0), 0, 8);
    case A_PR10H:
        return extract16(rl78_irq_read_priority_high(s, 0), 8, 8);
    case A_PR11L:
        return extract16(rl78_irq_read_priority_high(s, 1), 0, 8);
    case A_PR11H:
        return extract16(rl78_irq_read_priority_high(s, 1), 8, 8);
    default:
        g_assert_not_reached();
        return 0;
    }
}

static uint16_t rl78_irq_read0_word(RL78IRQControllerState *s, uint8_t offset)
{
    switch (offset) {
    case A_IF0:
        return rl78_irq_read_iflag(s, 0);
    case A_IF1:
        return rl78_irq_read_iflag(s, 1);
    case A_MK0:
        return rl78_irq_read_mask(s, 0);
    case A_MK1:
        return rl78_irq_read_mask(s, 1);
    case A_PR00:
        return rl78_irq_read_priority_low(s, 0);
    case A_PR01:
        return rl78_irq_read_priority_low(s, 1);
    case A_PR10:
        return rl78_irq_read_priority_high(s, 0);
    case A_PR11:
        return rl78_irq_read_priority_high(s, 1);
    default:
        g_assert_not_reached();
    }
}

static uint64_t rl78_irq_read0(void *opaque, hwaddr offset, unsigned size)
{
    RL78IRQControllerState *s = RL78_IRQ_CONTROLLER(opaque);

    switch (size) {
    case 1:
        return rl78_irq_read0_byte(s, offset);
    case 2:
        return rl78_irq_read0_word(s, offset);
    default:
        g_assert_not_reached();
    }
}

static void rl78_irq_write1_byte(RL78IRQControllerState *s, uint8_t offset,
                                 uint8_t data)
{
    switch (offset) {
    case A_IF2L:
        rl78_irq_update_iflag_lo_byte(s, 2, data);
        break;
    case A_IF2H:
        rl78_irq_update_iflag_hi_byte(s, 2, data);
        break;
    case A_IF3L:
        rl78_irq_update_iflag_lo_byte(s, 3, data);
        break;
    case A_IF3H:
        rl78_irq_update_iflag_hi_byte(s, 3, data);
        break;
    case A_MK2L:
        rl78_irq_update_mask_lo_byte(s, 2, data);
        break;
    case A_MK2H:
        rl78_irq_update_mask_hi_byte(s, 2, data);
        break;
    case A_MK3L:
        rl78_irq_update_mask_lo_byte(s, 3, data);
        break;
    case A_MK3H:
        rl78_irq_update_mask_hi_byte(s, 3, data);
        break;
    case A_PR02L:
        rl78_irq_update_priority_low_lo_byte(s, 2, data);
        break;
    case A_PR02H:
        rl78_irq_update_priority_low_hi_byte(s, 2, data);
        break;
    case A_PR03L:
        rl78_irq_update_priority_low_lo_byte(s, 3, data);
        break;
    case A_PR03H:
        rl78_irq_update_priority_low_hi_byte(s, 3, data);
        break;
    case A_PR12L:
        rl78_irq_update_priority_high_lo_byte(s, 2, data);
        break;
    case A_PR12H:
        rl78_irq_update_priority_high_hi_byte(s, 2, data);
        break;
    case A_PR13L:
        rl78_irq_update_priority_high_lo_byte(s, 3, data);
        break;
    case A_PR13H:
        rl78_irq_update_priority_high_hi_byte(s, 3, data);
        break;
    default:
        g_assert_not_reached();
        break;
    }
}

static void rl78_irq_write1_word(RL78IRQControllerState *s, uint8_t offset,
                                 uint16_t data)
{
    switch (offset) {
    case A_IF2:
        rl78_irq_update_iflag_word(s, 2, data);
        break;
    case A_IF3:
        rl78_irq_update_iflag_word(s, 3, data);
        break;
    case A_MK2:
        rl78_irq_update_mask_word(s, 2, data);
        break;
    case A_MK3:
        rl78_irq_update_mask_word(s, 3, data);
        break;
    case A_PR02:
        rl78_irq_update_priority_low(s, 2, data);
        break;
    case A_PR03:
        rl78_irq_update_priority_low(s, 3, data);
        break;
    case A_PR12:
        rl78_irq_update_priority_high(s, 2, data);
        break;
    case A_PR13:
        rl78_irq_update_priority_high(s, 3, data);
        break;
    default:
        g_assert_not_reached();
    }
}

static void rl78_irq_write1(void *opaque, hwaddr offset, uint64_t data,
                            unsigned size)
{
    RL78IRQControllerState *s = RL78_IRQ_CONTROLLER(opaque);

    switch (size) {
    case 1:
        rl78_irq_write1_byte(s, offset, data);
        break;
    case 2:
        rl78_irq_write1_word(s, offset, data);
        break;
    default:
        g_assert_not_reached();
        break;
    }
}

static uint16_t rl78_irq_read1_byte(RL78IRQControllerState *s, uint8_t offset)
{
    switch (offset) {
    case A_IF0L:
        return extract16(rl78_irq_read_iflag(s, 2), 0, 8);
    case A_IF0H:
        return extract16(rl78_irq_read_iflag(s, 2), 8, 8);
    case A_IF1L:
        return extract16(rl78_irq_read_iflag(s, 3), 0, 8);
    case A_IF1H:
        return extract16(rl78_irq_read_iflag(s, 3), 8, 8);
    case A_MK0L:
        return extract16(rl78_irq_read_mask(s, 2), 0, 8);
    case A_MK0H:
        return extract16(rl78_irq_read_mask(s, 2), 8, 8);
    case A_MK1L:
        return extract16(rl78_irq_read_mask(s, 3), 0, 8);
    case A_MK1H:
        return extract16(rl78_irq_read_mask(s, 3), 8, 8);
    case A_PR00L:
        return extract16(rl78_irq_read_priority_low(s, 2), 0, 8);
    case A_PR00H:
        return extract16(rl78_irq_read_priority_low(s, 2), 8, 8);
    case A_PR01L:
        return extract16(rl78_irq_read_priority_low(s, 3), 0, 8);
    case A_PR01H:
        return extract16(rl78_irq_read_priority_low(s, 3), 8, 8);
    case A_PR10L:
        return extract16(rl78_irq_read_priority_high(s, 2), 0, 8);
    case A_PR10H:
        return extract16(rl78_irq_read_priority_high(s, 2), 8, 8);
    case A_PR11L:
        return extract16(rl78_irq_read_priority_high(s, 3), 0, 8);
    case A_PR11H:
        return extract16(rl78_irq_read_priority_high(s, 3), 8, 8);
    default:
        g_assert_not_reached();
        return 0;
    }
}

static uint16_t rl78_irq_read1_word(RL78IRQControllerState *s, uint8_t offset)
{
    switch (offset) {
    case A_IF0:
        return rl78_irq_read_iflag(s, 2);
    case A_IF1:
        return rl78_irq_read_iflag(s, 3);
    case A_MK0:
        return rl78_irq_read_mask(s, 2);
    case A_MK1:
        return rl78_irq_read_mask(s, 3);
    case A_PR00:
        return rl78_irq_read_priority_low(s, 2);
    case A_PR01:
        return rl78_irq_read_priority_low(s, 3);
    case A_PR10:
        return rl78_irq_read_priority_high(s, 2);
    case A_PR11:
        return rl78_irq_read_priority_high(s, 3);
    default:
        g_assert_not_reached();
    }
}

static uint64_t rl78_irq_read1(void *opaque, hwaddr offset, unsigned size)
{
    RL78IRQControllerState *s = RL78_IRQ_CONTROLLER(opaque);

    switch (size) {
    case 1:
        return rl78_irq_read1_byte(s, offset);
    case 2:
        return rl78_irq_read1_word(s, offset);
    default:
        g_assert_not_reached();
    }
}

static void rl78_irq_write2(void *opaque, hwaddr offset, uint64_t data,
                            unsigned size)
{
    RL78IRQControllerState *s = RL78_IRQ_CONTROLLER(opaque);

    switch (offset) {
    case A_EGP0:
        rl78_irq_update_egp(s, 0, data);
        break;
    case A_EGN0:
        rl78_irq_update_egn(s, 0, data);
        break;
    case A_EGP1:
        rl78_irq_update_egp(s, 1, data);
        break;
    case A_EGN1:
        rl78_irq_update_egn(s, 1, data);
        break;
    default:
        g_assert_not_reached();
    }
}

static uint64_t rl78_irq_read2(void *opaque, hwaddr offset, unsigned size)
{
    RL78IRQControllerState *s = RL78_IRQ_CONTROLLER(opaque);
    switch (offset) {
    case A_EGP0:
        return rl78_irq_read_egp(s, 0);
    case A_EGN0:
        return rl78_irq_read_egn(s, 0);
    case A_EGP1:
        return rl78_irq_read_egp(s, 1);
    case A_EGN1:
        return rl78_irq_read_egn(s, 1);
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps rl78_irq_ops0 = {
    .write                 = rl78_irq_write0,
    .read                  = rl78_irq_read0,
    .valid.max_access_size = 2,
    .valid.min_access_size = 1,
    .impl.min_access_size  = 2,
    .impl.max_access_size  = 1,
};

static const MemoryRegionOps rl78_irq_ops1 = {
    .write                 = rl78_irq_write1,
    .read                  = rl78_irq_read1,
    .valid.max_access_size = 2,
    .valid.min_access_size = 1,
    .impl.min_access_size  = 2,
    .impl.max_access_size  = 1,
};

static const MemoryRegionOps rl78_irq_ops2 = {
    .write                 = rl78_irq_write2,
    .read                  = rl78_irq_read2,
    .valid.max_access_size = 1,
    .valid.min_access_size = 1,
    .impl.min_access_size  = 1,
    .impl.max_access_size  = 1,
};

static void rl78_irq_reset_hold(Object *obj, ResetType type)
{
    RL78IRQControllerState *s = RL78_IRQ_CONTROLLER(obj);

    s->irq_flag = 0;
    s->irq_mask = 0xFFFFFFFFFFFFFFFFULL;
    for (int i = 0; i < RL78_CPU_IRQ_NUM; i++) {
        s->irq_priority[i] = RL78_IRQ_PRIORITY_NUM - 1;
    }

    for (int i = 0; i < RL78_IRQ_EXTERNAL_PIN_NUM; i++) {
        s->edge_types[i] = RL78_IRQ_EXTINT_EDGE_TYPE_FORBIDDEN;
    }

    rl78_irq_set_irq(s);
}

static void rl78_irq_ack_irq(void *opaque, int irq, int level)
{
    RL78IRQControllerState *s = RL78_IRQ_CONTROLLER(opaque);

    assert(level >= 0 && level < RL78_CPU_IRQ_NUM);
    s->irq_flag &= ~(1ULL << level);

    rl78_irq_set_irq(s);
}

static void rl78_irq_recv_irq(void *opaque, int irq, int level)
{
    RL78IRQControllerState *s = RL78_IRQ_CONTROLLER(opaque);

    s->irq_flag |= 1ULL << irq;

    rl78_irq_set_irq(s);
}

static void rl78_irq_init(Object *obj)
{
    DeviceState *dev          = DEVICE(obj);
    RL78IRQControllerState *s = RL78_IRQ_CONTROLLER(obj);

    memory_region_init_io(&s->mmio[0], OBJECT(s), &rl78_irq_ops0, s,
                          "rl78-irq-mmio[0]", 0x10);
    memory_region_init_io(&s->mmio[1], OBJECT(s), &rl78_irq_ops1, s,
                          "rl78-irq-mmio[1]", 0x10);
    memory_region_init_io(&s->mmio[2], OBJECT(s), &rl78_irq_ops2, s,
                          "rl78-irq-mmio[2]", 0x04);

    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio[0]);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio[1]);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio[2]);

    qdev_init_gpio_out_named(dev, &s->irq_req, "irq-req", 1);
    qdev_init_gpio_in_named(dev, rl78_irq_ack_irq, "irq-ack", 1);
    qdev_init_gpio_in_named(dev, rl78_irq_recv_irq, "irq-in", RL78_CPU_IRQ_NUM);
}

static void rl78_irq_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc        = RESETTABLE_CLASS(klass);
    RL78IRQControllerClass *ic = RL78_IRQ_CONTROLLER_CLASS(klass);

    resettable_class_set_parent_phases(rc, NULL, rl78_irq_reset_hold, NULL,
                                       &ic->parent_phases);
}

static const TypeInfo rl78_irq_info = {
    .name = TYPE_RL78_IRQ_CONTROLLER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RL78IRQControllerState),
    .instance_init = rl78_irq_init,
    .class_size = sizeof(RL78IRQControllerClass),
    .class_init = rl78_irq_class_init,
};

static void rl78_irq_register_types(void)
{
    type_register_static(&rl78_irq_info);
}

type_init(rl78_irq_register_types)
