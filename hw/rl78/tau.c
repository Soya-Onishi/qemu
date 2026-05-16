#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/irq.h"
#include "hw/core/registerfields.h"
#include "hw/core/clock.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/resettable.h"
#include "qemu/timer.h"
#include "hw/rl78/tau.h"

struct RL78TAUClass {
    /* private */
    SysBusDeviceClass parent_class;

    /* public */
    ResettablePhases parent_phases;
};
typedef struct RL78TAUClass RL78TAUClass;

DECLARE_CLASS_CHECKERS(RL78TAUClass, RL78_TAU, TYPE_RL78_TAU)

REG16(TDR, 0x00)

REG16(TPS, 0x36)
FIELD(TPS, PRS0, 0, 4)
FIELD(TPS, PRS1, 4, 4)
FIELD(TPS, PRS2, 8, 2)
FIELD(TPS, PRS3, 12, 2)

REG16(TMR, 0x00)
FIELD(TMR, MD0, 0, 1)
FIELD(TMR, MD1, 1, 3)
FIELD(TMR, CIS, 6, 2)
FIELD(TMR, STS, 8, 3)
FIELD(TMR, MASTER, 11, 1)
FIELD(TMR, SPLIT, 11, 1)
FIELD(TMR, CCS, 12, 1)
FIELD(TMR, CKS, 14, 2)

REG16(TSR, 0x20)
FIELD(TSR, OVF, 0, 1)

REG16(TE, 0x30)
REG16(TS, 0x32)
REG16(TT, 0x34)

REG8(TIS0, 0x00)
FIELD(TIS0, TIS, 0, 3)

REG8(TIS1, 0x01)
FIELD(TIS1, TIS0, 0, 1)
FIELD(TIS1, TIS1, 1, 1)

REG16(TOE, 0x3A)
REG16(TO, 0x38)
REG16(TOL, 0x3C)
REG16(TOM, 0x3E)

static double rl78_tau_clock_duration(RL78TAUState *s, const uint8_t channel)
{
    const double inclk_hz    = clock_get_hz(s->inclk);
    const uint32_t ck_select = s->tmr[channel].clock_select;
    const double clock_hz    = inclk_hz / s->clk_divider[ck_select];
    const double clock_sec   = 1.0 / clock_hz;

    return clock_sec;
}

static uint16_t rl78_tau_remain_count(QEMUTimer *timer,
                                      const double clock_duration)
{
    const uint64_t remain_ns =
        timer_expire_time_ns(timer) - qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    const double clock_ns       = clock_duration * 1000 * 1000 * 1000;
    const uint16_t remain_count = (uint16_t)((double)remain_ns / clock_ns);

    return remain_count;
}

static void rl78_tau_update_tcr(RL78TAUState *s, const uint16_t value)
{
    qemu_log_mask(LOG_GUEST_ERROR, "TCR is readonly register\n");
}

static void rl78_tau_update_tdr_16bit(RL78TAUState *s, const uint16_t value,
                                      const uint8_t channel)
{
    if (s->tmr[channel].use_split) {
        s->channel[channel].tdr.bytes[0] = (uint8_t)((value >> 0) & 0xFF);
        s->channel[channel].tdr.bytes[1] = (uint8_t)((value >> 8) & 0xFF);
    } else {
        s->channel[channel].tdr.word = value;
    }
}

static void rl78_tau_update_tdr_8bit_lo(RL78TAUState *s, const uint8_t value,
                                        const uint8_t channel)
{
    if (channel != 1 && channel != 3) {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "8bit access is not allowed for Channel3, 5: channel: %d\n",
            channel);
        return;
    }

    if (!s->tmr[channel].use_split) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "8bit access is allowed only when TMR SPLIT bit is 1.\n");
        return;
    }

    s->channel[channel].tdr.bytes[0] = value;
}

static void rl78_tau_update_tdr_8bit_hi(RL78TAUState *s, const uint8_t value,
                                        const uint8_t channel)
{
    if (channel != 1 && channel != 3) {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "8bit access is not allowed for Channel3, 5: channel: %d\n",
            channel);
        return;
    }

    if (!s->tmr[channel].use_split) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "8bit access is allowed only when TMR SPLIT bit is 0.\n");
        return;
    }

    s->channel[channel].tdr.bytes[1] = value;
}

static void rl78_tau_update_tps(RL78TAUState *s, uint16_t value)
{
    // TODO: assert value
    const uint8_t prs0 = FIELD_EX16(value, TPS, PRS0);
    const uint8_t prs1 = FIELD_EX16(value, TPS, PRS1);
    const uint8_t prs2 = FIELD_EX16(value, TPS, PRS2);
    const uint8_t prs3 = FIELD_EX16(value, TPS, PRS3);

    // TODO: forbid updates if channels use corresponding clock divider.
    s->clk_divider[0] = 1 << prs0;
    s->clk_divider[1] = 1 << prs1;
    s->clk_divider[2] = prs2 == 0 ? 2 : (1 << (prs2 * 2));
    s->clk_divider[3] = 1 << (8 + prs3 * 2);

    s->tps = value;
}

static void rl78_tau_update_tmr(RL78TAUState *s, const uint16_t value,
                                const uint8_t channel)
{
    // TODO: assert value
    const bool split  = FIELD_EX16(value, TMR, SPLIT);
    const bool master = FIELD_EX16(value, TMR, MASTER);

    // If timer mode is updated, TCR is reset.
    if (s->tmr[channel].mode != FIELD_EX16(value, TMR, MD1)) {
        // TODO: support other than interval timer mode.
        s->channel[channel].tcr.word = 0xFFFF;
    }

    s->tmr[channel].mode           = FIELD_EX16(value, TMR, MD1);
    s->tmr[channel].timer_behavior = FIELD_EX16(value, TMR, MD0);
    s->tmr[channel].input_edge     = FIELD_EX16(value, TMR, CIS);
    s->tmr[channel].start_trigger  = FIELD_EX16(value, TMR, CCS);
    s->tmr[channel].use_ti         = FIELD_EX16(value, TMR, CCS);

    switch (FIELD_EX16(value, TMR, CKS)) {
    default:
    case 0:
        s->tmr[channel].clock_select = 0;
        break;
    case 1:
        s->tmr[channel].clock_select = 2;
        break;
    case 2:
        s->tmr[channel].clock_select = 1;
        break;
    case 3:
        s->tmr[channel].clock_select = 3;
        break;
    }

    switch (channel) {
    default:
    case 0:
    case 5:
    case 7:
        s->tmr[channel].is_master = false;
        s->tmr[channel].use_split = false;
        break;
    case 1:
    case 3:
        s->tmr[channel].is_master = false;
        s->tmr[channel].use_split = split;
        break;
    case 2:
    case 4:
    case 6:
        s->tmr[channel].is_master = master;
        s->tmr[channel].use_split = false;
        break;
    }
}

static void rl78_tau_update_tsr(RL78TAUState *s, const uint16_t value)
{
    qemu_log_mask(LOG_GUEST_ERROR, "TSR is readonly register\n");
}

static void rl78_tau_update_te(RL78TAUState *s, const uint16_t value)
{
    qemu_log_mask(LOG_GUEST_ERROR, "TE is readonly register\n");
}

static void rl78_tau_start_timer(RL78TAUState *s, const uint8_t channel,
                                 const bool is_high)
{
    const double clock_duration = rl78_tau_clock_duration(s, channel);
    const double clock_ns       = clock_duration * 1000 * 1000 * 1000;
    if (is_high) {
        const uint64_t count    = s->channel[channel].tdr.bytes[1];
        const uint64_t timer_ns = (uint64_t)(clock_ns * (count + 1));
        const uint64_t expire_time =
            qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + timer_ns;

        s->channel[channel].last_high_timer_expire = expire_time;
        timer_mod(&s->channel[channel].high_timer, expire_time);

        if (s->tmr[channel].timer_behavior == 1) {
            qemu_set_irq(s->high_irqs[channel], 1);
        }
    } else {
        const uint64_t count    = s->tmr[channel].use_split
                                      ? s->channel[channel].tdr.bytes[0]
                                      : s->channel[channel].tdr.word;
        const uint64_t timer_ns = (uint64_t)(clock_ns * (count + 1));
        const uint64_t expire_time =
            qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + timer_ns;

        s->channel[channel].last_timer_expire = expire_time;
        const uint64_t before_expire_time = timer_expire_time_ns(&s->channel[channel].timer);
        const uint64_t after_expire_time = expire_time;
        const uint64_t expire_time_diff = after_expire_time - before_expire_time;
        const uint64_t squezed_timerup_count = expire_time_diff / clock_ns;

        if(squezed_timerup_count > 2) {
            qemu_log("before: %lu, after: %lu, diff: %lu, squezed_timerup_count: %lu\n", before_expire_time, after_expire_time, expire_time_diff, squezed_timerup_count);
        }

        timer_mod(&s->channel[channel].timer, expire_time);

        if (s->tmr[channel].timer_behavior == 1) {
            qemu_set_irq(s->irqs[channel], 1);
        }
    }
}

static void rl78_tau_update_ts(RL78TAUState *s, const uint16_t value)
{
    // In RL78 hardware manual, delays are required if switching
    // from fuctions which do not use TImn to functions which use TImn.
    // However, in QEMU, no delays are required for simplicity.

    if (unlikely(value & 0xF500)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TS register, bit15-12, 10, 8 must be 0. value: 0x%04X\n",
                      value);
    }

    const uint16_t written_value = value & 0x0AFF;
    for (int i = 0; i < RL78_TAU_CHANNEL_NUM; i++) {
        if (written_value & (1 << i)) {
            if (!s->channel[i].enabled) {
                rl78_tau_start_timer(s, i, false);
            }

            s->channel[i].enabled = true;
        }

        if (written_value & (1 << (i + 8))) {
            if (!s->channel[i].high_enabled) {
                rl78_tau_start_timer(s, i, true);
            }

            s->channel[i].high_enabled = true;
        }
    }
}

static void rl78_tau_stop_timer(RL78TAUState *s, const uint8_t channel,
                                const bool is_high)
{
    // TODO: support other than interval timer mode.
    const double clock_duration = rl78_tau_clock_duration(s, channel);

    if (is_high) {
        s->channel[channel].tcr.bytes[1] = rl78_tau_remain_count(
            &s->channel[channel].high_timer, clock_duration);

        timer_del(&s->channel[channel].high_timer);
    } else {
        const uint64_t remain_count =
            rl78_tau_remain_count(&s->channel[channel].timer, clock_duration);
        if (s->tmr[channel].use_split) {
            s->channel[channel].tcr.bytes[0] = remain_count;
        } else {
            s->channel[channel].tcr.word = remain_count;
        }

        timer_del(&s->channel[channel].timer);
    }
}

static void rl78_tau_update_tt(RL78TAUState *s, const uint16_t value)
{
    if (unlikely(value & 0xF500)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TT register, bit15-12, 10, 8 must be 0. value: 0x%04X\n",
                      value);
    }

    const uint16_t written_value = value & 0x0AFF;
    for (int i = 0; i < RL78_TAU_CHANNEL_NUM; i++) {
        if (written_value & (1 << i)) {
            s->channel[i].enabled = false;
            rl78_tau_stop_timer(s, i, false);
        }

        if (written_value & (1 << (i + 8))) {
            s->channel[i].high_enabled = false;
            rl78_tau_stop_timer(s, i, true);
        }
    }
}

static void rl78_tau_update_tis0(RL78TAUState *s, const uint8_t value)
{
    if (unlikely(value & 0xF8)) {
        // In hardware manual, there are no notes about bit7-3,
        // but it is not reasonable to be bit7-3 is high,
        // so error is logged when bit7-3 is high.
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TIS0 register, bit7-3 must be 0. value: 0x%02X\n",
                      value);
    }

    s->channel[5].input_type = FIELD_EX8(value, TIS0, TIS);
}

static void rl78_tau_update_tis1(RL78TAUState *s, const uint8_t value)
{
    if (unlikely(value & 0xFC)) {
        // In hardware manual, there are no notes about bit7-2,
        // but it is not reasonable to be bit7-2 is high,
        // so error is logged when bit7-2 is high.
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TIS1 register, bit7-2 must be 0. value: 0x%02X\n",
                      value);
    }

    s->channel[0].input_type = FIELD_EX8(value, TIS1, TIS0);
    s->channel[1].input_type = FIELD_EX8(value, TIS1, TIS1);
}

static void rl78_tau_update_toe(RL78TAUState *s, const uint16_t value)
{
    if (unlikely(value & 0xFF00)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TOE register, bit15-8 must be 0. value: 0x%04X\n",
                      value);
    }

    for (int i = 0; i < RL78_TAU_CHANNEL_NUM; i++) {
        s->channel[i].output_enabled = !!(value & (1 << i));
    }
}

static void rl78_tau_update_to(RL78TAUState *s, const uint16_t value)
{
    if (unlikely(value & 0xFF00)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TO register, bit15-8 must be 0. value: 0x%04X\n", value);
    }

    for (int i = 0; i < RL78_TAU_CHANNEL_NUM; i++) {
        const bool b = !!(value & (1 << i));

        if (s->channel[i].output_enabled && b != s->channel[i].output) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "TOE bit%d must be 0 if overwrite. value: 0x%04X\n",
                          i, value);
            continue;
        }

        s->channel[i].output = b;
    }
}

static void rl78_tau_update_tol(RL78TAUState *s, const uint16_t value)
{
    if (unlikely(value & 0xFF00)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TOL register, bit15-8 must be 0. value: 0x%04X\n",
                      value);
    }

    for (int i = 0; i < RL78_TAU_CHANNEL_NUM; i++) {
        if (value & (1 << i)) {
            s->channel[i].toggle_output = true;
        }
    }
}

static void rl78_tau_update_tom(RL78TAUState *s, const uint16_t value)
{
    if (unlikely(value & 0xFF01)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TOM register, bit15-8, 0 must be 0. value: 0x%04X\n",
                      value);
    }

    const uint16_t written_value = value & 0x00FE;
    for (int i = 0; i < RL78_TAU_CHANNEL_NUM; i++) {
        s->channel[i].is_slave = !!(written_value & (1 << i));
    }
}

static uint16_t rl78_tau_read_tcr(RL78TAUState *s, const uint8_t channel)
{
    // TODO: after stopping timer by TT and changing TMR mode, TCR has invalid
    // value. Assert it.
    // TODO: support other than interval timer mode.

    const double clock_duration = rl78_tau_clock_duration(s, channel);
    if (s->tmr[channel].use_split) {
        uint16_t lo = s->channel[channel].enabled
                          ? rl78_tau_remain_count(&s->channel[channel].timer,
                                                  clock_duration)
                          : s->channel[channel].tcr.bytes[0];
        uint16_t hi = s->channel[channel].high_enabled
                          ? rl78_tau_remain_count(
                                &s->channel[channel].high_timer, clock_duration)
                          : s->channel[channel].tcr.bytes[1];

        return (hi << 8) | lo;
    } else {
        return s->channel[channel].enabled
                   ? rl78_tau_remain_count(&s->channel[channel].timer,
                                           clock_duration)
                   : s->channel[channel].tcr.word;
    }
}

static uint16_t rl78_tau_read_tdr_8bit_lo(RL78TAUState *s,
                                          const uint8_t channel)
{
    if (channel != 1 && channel != 3) {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "8bit access is allowed only for Channel3, 5: channel: %d\n",
            channel);
        return 0;
    }

    if (!s->tmr[channel].use_split) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "8bit access is allowed only when TMR SPLIT bit is 1.\n");
        return 0;
    }

    return (uint16_t)(s->channel[channel].tdr.bytes[0]);
}

static uint16_t rl78_tau_read_tdr_8bit_hi(RL78TAUState *s,
                                          const uint8_t channel)
{
    if (channel != 1 && channel != 3) {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "8bit access is allowed only for Channel3, 5: channel: %d\n",
            channel);
        return 0;
    }

    if (!s->tmr[channel].use_split) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "8bit access is allowed only when TMR SPLIT bit is 0.\n");
        return 0;
    }

    return (uint16_t)(s->channel[channel].tdr.bytes[1]);
}

static uint16_t rl78_tau_read_tdr_16bit(RL78TAUState *s, const uint8_t channel)
{
    if (s->tmr[channel].use_split) {
        const uint16_t lo = (uint16_t)(s->channel[channel].tdr.bytes[0]);
        const uint16_t hi = (uint16_t)(s->channel[channel].tdr.bytes[1]);

        return (hi << 8) | lo;
    } else {
        return s->channel[channel].tdr.word;
    }
}

static uint16_t rl78_tau_read_tps(RL78TAUState *s) { return s->tps; }

static uint16_t rl78_tau_read_tmr(RL78TAUState *s, const uint8_t channel)
{
    uint16_t value = 0;

    value = FIELD_DP16(value, TMR, MD1, s->tmr[channel].mode);
    value = FIELD_DP16(value, TMR, MD0, s->tmr[channel].timer_behavior);
    value = FIELD_DP16(value, TMR, CIS, s->tmr[channel].input_edge);
    value = FIELD_DP16(value, TMR, STS, s->tmr[channel].start_trigger);
    value = FIELD_DP16(value, TMR, CCS, s->tmr[channel].use_ti);
    value = FIELD_DP16(value, TMR, CKS, s->tmr[channel].clock_select);

    switch (channel) {
    default:
    case 0:
    case 5:
    case 7:
        value = FIELD_DP16(value, TMR, MASTER, 0);
        break;
    case 1:
    case 3:
        value = FIELD_DP16(value, TMR, SPLIT, s->tmr[channel].use_split);
        break;
    case 2:
    case 4:
    case 6:
        value = FIELD_DP16(value, TMR, MASTER, s->tmr[channel].is_master);
        break;
    }

    return value;
}

static uint16_t rl78_tau_read_tsr(RL78TAUState *s, const uint8_t channel)
{
    const RL78TAUTimerMode mode = s->tmr[channel].mode;
    if (mode != RL78_TAU_TIMER_MODE_CAPTURE &&
        mode != RL78_TAU_TIMER_MODE_CAPTURE_AND_ONE_COUNT) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "If not in Capture mode or Capture & Onecount mode, TSR "
                      "register is always 0.\n");
    }

    uint16_t value = 0;
    value = FIELD_DP16(value, TSR, OVF, s->channel[0].overflow_occurred);

    return value;
}

static uint16_t rl78_tau_read_te(RL78TAUState *s)
{
    uint16_t value = 0;

    for (int i = 0; i < RL78_TAU_CHANNEL_NUM; i++) {
        value |= (uint16_t)(s->channel[i].enabled) << i;
        value |= (uint16_t)(s->channel[i].high_enabled) << (i + 8);
    }

    return value;
}

static uint16_t rl78_tau_read_ts(RL78TAUState *s)
{
    // There is no delay to negate TS bit.
    return 0;
}

static uint16_t rl78_tau_read_tt(RL78TAUState *s)
{
    // There is no delay to negate TT bit.
    return 0;
}

static uint8_t rl78_tau_read_tis0(RL78TAUState *s)
{
    return FIELD_DP8(0, TIS0, TIS, s->channel[5].input_type);
}

static uint8_t rl78_tau_read_tis1(RL78TAUState *s)
{
    uint8_t value = 0;

    value = FIELD_DP8(value, TIS1, TIS0, s->channel[0].input_type);
    value = FIELD_DP8(value, TIS1, TIS1, s->channel[1].input_type);

    return value;
}

static uint16_t rl78_tau_read_toe(RL78TAUState *s)
{
    uint16_t value = 0;

    for (int i = 0; i < RL78_TAU_CHANNEL_NUM; i++) {
        value |= (uint16_t)(s->channel[i].output_enabled) << i;
    }

    return value;
}

static uint16_t rl78_tau_read_to(RL78TAUState *s)
{
    uint16_t value = 0;

    for (int i = 0; i < RL78_TAU_CHANNEL_NUM; i++) {
        value |= (uint16_t)(s->channel[i].output) << i;
    }

    return value;
}

static uint16_t rl78_tau_read_tol(RL78TAUState *s)
{
    uint16_t value = 0;

    for (int i = 0; i < RL78_TAU_CHANNEL_NUM; i++) {
        value |= (uint16_t)(s->channel[i].toggle_output) << i;
    }

    return value;
}

static uint16_t rl78_tau_read_tom(RL78TAUState *s)
{
    uint16_t value = 0;

    for (int i = 0; i < RL78_TAU_CHANNEL_NUM; i++) {
        value |= (uint16_t)(s->channel[i].is_slave) << i;
    }

    return value;
}

static void rl78_tau_write_tdr(RL78TAUState *s, hwaddr offset, uint64_t data,
                               unsigned size)
{
    switch (size) {
    case 1: {
        const uint8_t ch = offset / 2;
        const bool is_hi = !!(offset % 2);

        if (is_hi) {
            rl78_tau_update_tdr_8bit_hi(s, (uint8_t)data, ch);
        } else {
            rl78_tau_update_tdr_8bit_lo(s, (uint8_t)data, ch);
        }
    } break;
    case 2: {
        const uint8_t ch = offset / 2;
        rl78_tau_update_tdr_16bit(s, (uint16_t)data, ch);
        break;
    }
    default:
        break;
    }
}

static uint64_t rl78_tau_read_tdr(RL78TAUState *s, hwaddr offset, unsigned size)
{
    switch (size) {
    case 1: {
        const uint8_t ch = offset / 2;
        const bool is_hi = !!(offset % 2);

        if (is_hi) {
            return rl78_tau_read_tdr_8bit_hi(s, ch);
        } else {
            return rl78_tau_read_tdr_8bit_lo(s, ch);
        }
    }
    case 2: {
        const uint8_t ch = offset / 2;
        return rl78_tau_read_tdr_16bit(s, ch);
    }
    default:
        return 0;
    }
}

static void rl78_tau_write0(void *opaque, hwaddr offset, uint64_t data,
                            unsigned size)
{
    // for TDRm0-1
    RL78TAUState *s = RL78_TAU(opaque);
    rl78_tau_write_tdr(s, offset, data, size);
}

static uint64_t rl78_tau_read0(void *opaque, hwaddr offset, unsigned size)
{
    // for TDRm0-1
    RL78TAUState *s = RL78_TAU(opaque);
    return rl78_tau_read_tdr(s, offset, size);
}

static void rl78_tau_write1(void *opaque, hwaddr offset, uint64_t data,
                            unsigned size)
{
    RL78TAUState *s = RL78_TAU(opaque);
    rl78_tau_write_tdr(s, offset, data, size);
}

static uint64_t rl78_tau_read1(void *opaque, hwaddr offset, unsigned size)
{
    // for TDRm2-7
    RL78TAUState *s = RL78_TAU(opaque);
    return rl78_tau_read_tdr(s, offset + 4, size);
}

static void rl78_tau_write2(void *opaque, hwaddr offset, uint64_t data,
                            unsigned size)
{
    // for other TAU registers
    RL78TAUState *s       = RL78_TAU(opaque);
    const uint8_t channel = (offset / 2) % RL78_TAU_CHANNEL_NUM;

    switch (offset) {
    case 0x00:
    case 0x02:
    case 0x04:
    case 0x06:
    case 0x08:
    case 0x0A:
    case 0x0C:
    case 0x0E:
        if (size == 1) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "8bit access is not allowed for TCR register.\n");
            return;
        }

        rl78_tau_update_tcr(s, (uint16_t)data);
        break;
    case 0x10:
    case 0x12:
    case 0x14:
    case 0x16:
    case 0x18:
    case 0x1A:
    case 0x1C:
    case 0x1E:
        if (size == 1) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "8bit access is not allowed for TMR register.\n");
            return;
        }

        rl78_tau_update_tmr(s, (uint16_t)data, channel);
        break;
    case 0x20:
    case 0x22:
    case 0x24:
    case 0x26:
    case 0x28:
    case 0x2A:
    case 0x2C:
    case 0x2E:
        rl78_tau_update_tsr(s, (uint16_t)data);
        break;
    case 0x30:
        rl78_tau_update_te(s, (uint16_t)data);
        break;
    case 0x32:
        rl78_tau_update_ts(s, (uint16_t)data);
        break;
    case 0x34:
        rl78_tau_update_tt(s, (uint16_t)data);
        break;
    case 0x36:
        if (size == 1) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "8bit access is not allowed for TPS register.\n");
            return;
        }

        rl78_tau_update_tps(s, (uint16_t)data);
        break;
    case 0x38:
        rl78_tau_update_to(s, (uint16_t)data);
        break;
    case 0x3A:
        rl78_tau_update_toe(s, (uint16_t)data);
        break;
    case 0x3C:
        rl78_tau_update_tol(s, (uint16_t)data);
        break;
    case 0x3E:
        rl78_tau_update_tom(s, (uint16_t)data);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid access address\n");
        break;
    }
}

static uint64_t rl78_tau_read2(void *opaque, hwaddr offset, unsigned size)
{
    // for other TAU registers
    RL78TAUState *s       = RL78_TAU(opaque);
    const uint8_t channel = (offset / 2) % RL78_TAU_CHANNEL_NUM;
    switch (offset) {
    case 0x00:
    case 0x02:
    case 0x04:
    case 0x06:
    case 0x08:
    case 0x0A:
    case 0x0C:
    case 0x0E:
        if (size == 1) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "8bit access is not allowed for TCR register.\n");
            return 0;
        }

        return rl78_tau_read_tcr(s, channel);
    case 0x10:
    case 0x12:
    case 0x14:
    case 0x16:
    case 0x18:
    case 0x1A:
    case 0x1C:
    case 0x1E:
        if (size == 1) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "8bit access is not allowed for TMR register.\n");
            return 0;
        }

        return rl78_tau_read_tmr(s, channel);
    case 0x20:
    case 0x22:
    case 0x24:
    case 0x26:
    case 0x28:
    case 0x2A:
    case 0x2C:
    case 0x2E:
        return rl78_tau_read_tsr(s, channel);
    case 0x30:
        return rl78_tau_read_te(s);
    case 0x32:
        return rl78_tau_read_ts(s);
    case 0x34:
        return rl78_tau_read_tt(s);
    case 0x36:
        if (size == 1) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "8bit access is not allowed for TPS register.\n");
            return 0;
        }

        return rl78_tau_read_tps(s);
    case 0x38:
        return rl78_tau_read_to(s);
    case 0x3A:
        return rl78_tau_read_toe(s);
    case 0x3C:
        return rl78_tau_read_tol(s);
    case 0x3E:
        return rl78_tau_read_tom(s);
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid access address\n");
        return 0;
    }
}

static void rl78_tau_write3(void *opaque, hwaddr offset, uint64_t data,
                            unsigned size)
{
    RL78TAUState *s = RL78_TAU(opaque);
    switch (offset) {
    case 0x00:
        rl78_tau_update_tis0(s, (uint8_t)data);
        break;
    case 0x01:
        rl78_tau_update_tis1(s, (uint8_t)data);
        break;
    default:
        break;
    }
}

static uint64_t rl78_tau_read3(void *opaque, hwaddr offset, unsigned size)
{
    RL78TAUState *s = RL78_TAU(opaque);
    switch (offset) {
    case 0x00:
        return rl78_tau_read_tis0(s);
    case 0x01:
        return rl78_tau_read_tis1(s);
    default:
        return 0;
    }
}

// for TDRm0-1
static const MemoryRegionOps rl78_tau_ops0 = {
    .write                 = rl78_tau_write0,
    .read                  = rl78_tau_read0,
    .valid.max_access_size = 2,
    .valid.min_access_size = 1,
    .impl.min_access_size  = 2,
    .impl.max_access_size  = 1,
};

// for TDRm2-7
static const MemoryRegionOps rl78_tau_ops1 = {
    .write                 = rl78_tau_write1,
    .read                  = rl78_tau_read1,
    .valid.max_access_size = 2,
    .valid.min_access_size = 1,
    .impl.min_access_size  = 2,
    .impl.max_access_size  = 1,
};

// for other TAU registers
static const MemoryRegionOps rl78_tau_ops2 = {
    .write                 = rl78_tau_write2,
    .read                  = rl78_tau_read2,
    .valid.max_access_size = 2,
    .valid.min_access_size = 1,
    .impl.min_access_size  = 2,
    .impl.max_access_size  = 1,
};

// for TISx registers
static const MemoryRegionOps rl78_tau_ops3 = {
    .write                 = rl78_tau_write3,
    .read                  = rl78_tau_read3,
    .valid.max_access_size = 1,
    .valid.min_access_size = 1,
    .impl.min_access_size  = 1,
    .impl.max_access_size  = 1,
};

static void rl78_tau_update_timer(QEMUTimer *timer, uint64_t *last_expire_time,
                                  const uint16_t count,
                                  const double clock_duration)
{
    const double timer_duration = clock_duration * (count + 1);
    const uint64_t timer_ns = (uint64_t)(timer_duration * 1000 * 1000 * 1000);
    const uint64_t expire_time = *last_expire_time + timer_ns;


    *last_expire_time = expire_time;
    timer_mod(timer, expire_time);
}

static void rl78_tau_txend_interval_timer(QEMUTimer *timer,
                                          uint64_t *last_expire_time,
                                          const qemu_irq irq,
                                          const uint16_t count,
                                          const double clock_duration)
{
    qemu_set_irq(irq, 1);
    rl78_tau_update_timer(timer, last_expire_time, count, clock_duration);
}

static void rl78_tau_txend(RL78TAUState *s, const uint8_t channel)
{
    // TODO: support other than interval timer mode.
    const double clock_duration = rl78_tau_clock_duration(s, channel);
    const uint16_t count        = s->tmr[channel].use_split
                                      ? s->channel[channel].tdr.bytes[0]
                                      : s->channel[channel].tdr.word;
    rl78_tau_txend_interval_timer(&s->channel[channel].timer,
                                  &s->channel[channel].last_timer_expire,
                                  s->irqs[channel], count, clock_duration);
}

static void rl78_tau_txend_high(RL78TAUState *s, const uint8_t channel)
{
    // TODO: support other than interval timer mode.
    const double clock_duration = rl78_tau_clock_duration(s, channel);
    const uint16_t count        = s->channel[channel].tdr.bytes[1];

    rl78_tau_txend_interval_timer(&s->channel[channel].high_timer,
                                  &s->channel[channel].last_high_timer_expire,
                                  s->high_irqs[channel], count, clock_duration);
}

#define RL78_TAU_TXEND_CALLBACK(channel_num)                                   \
    static void rl78_tau_txend_channel##channel_num(void *opaque)              \
    {                                                                          \
        RL78TAUState *s = RL78_TAU(opaque);                                    \
        rl78_tau_txend(s, channel_num);                                        \
    }

#define RL78_TAU_TXEND_CALLBACK_HIGH(channel_num)                              \
    static void rl78_tau_txend_channel_high##channel_num(void *opaque)         \
    {                                                                          \
        RL78TAUState *s = RL78_TAU(opaque);                                    \
        rl78_tau_txend_high(s, channel_num);                                   \
    }

RL78_TAU_TXEND_CALLBACK(0)
RL78_TAU_TXEND_CALLBACK(1)
RL78_TAU_TXEND_CALLBACK(2)
RL78_TAU_TXEND_CALLBACK(3)
RL78_TAU_TXEND_CALLBACK(4)
RL78_TAU_TXEND_CALLBACK(5)
RL78_TAU_TXEND_CALLBACK(6)
RL78_TAU_TXEND_CALLBACK(7)

RL78_TAU_TXEND_CALLBACK_HIGH(0)
RL78_TAU_TXEND_CALLBACK_HIGH(1)
RL78_TAU_TXEND_CALLBACK_HIGH(2)
RL78_TAU_TXEND_CALLBACK_HIGH(3)
RL78_TAU_TXEND_CALLBACK_HIGH(4)
RL78_TAU_TXEND_CALLBACK_HIGH(5)
RL78_TAU_TXEND_CALLBACK_HIGH(6)
RL78_TAU_TXEND_CALLBACK_HIGH(7)

static void (*rl78_tau_txend_callbacks[RL78_TAU_CHANNEL_NUM])(void *) = {
    rl78_tau_txend_channel0, rl78_tau_txend_channel1, rl78_tau_txend_channel2,
    rl78_tau_txend_channel3, rl78_tau_txend_channel4, rl78_tau_txend_channel5,
    rl78_tau_txend_channel6, rl78_tau_txend_channel7,
};

static void (*rl78_tau_txend_callbacks_high[RL78_TAU_CHANNEL_NUM])(void *) = {
    rl78_tau_txend_channel_high0, rl78_tau_txend_channel_high1,
    rl78_tau_txend_channel_high2, rl78_tau_txend_channel_high3,
    rl78_tau_txend_channel_high4, rl78_tau_txend_channel_high5,
    rl78_tau_txend_channel_high6, rl78_tau_txend_channel_high7,
};

static void rl78_tau_reset_hold(Object *obj, ResetType type)
{
    RL78TAUState *s = RL78_TAU(obj);

    s->tps            = 0x0000;
    s->clk_divider[0] = 1;
    s->clk_divider[1] = 1;
    s->clk_divider[2] = 2;
    s->clk_divider[3] = 8;

    for (int i = 0; i < RL78_TAU_CHANNEL_NUM; i++) {
        s->tmr[i].mode           = RL78_TAU_TIMER_MODE_INTERVAL;
        s->tmr[i].timer_behavior = 0;
        s->tmr[i].input_edge     = RL78_TAU_INPUT_EDGE_RISING;
        s->tmr[i].start_trigger  = RL78_TAU_START_TRIGGER_SOFTWARE;
        s->tmr[i].use_ti         = false;
        s->tmr[i].clock_select   = 0;
        s->tmr[i].use_split      = false;
        s->tmr[i].is_master      = false;

        s->channel[i].tcr.word = 0xFFFF;
        s->channel[i].tdr.word = 0x0000;

        s->channel[i].overflow_occurred = false;
        s->channel[i].enabled           = false;
        s->channel[i].high_enabled      = false;
        s->channel[i].output_enabled    = false;
        s->channel[i].output            = false;
        s->channel[i].toggle_output     = false;
        s->channel[i].is_slave          = false;

        s->channel[i].input_type = RL78_TAU_TIMER_INPUT_TI;

        timer_del(&s->channel[i].timer);
        timer_del(&s->channel[i].high_timer);
    }
}

static void rl78_tau_init(Object *obj)
{
    DeviceState *dev  = DEVICE(obj);
    SysBusDevice *sys = SYS_BUS_DEVICE(dev);
    RL78TAUState *s   = RL78_TAU(obj);

    s->inclk = qdev_init_clock_in(dev, "inclk", NULL, dev, ClockUpdate);

    memory_region_init_io(&s->mmio[0], OBJECT(s), &rl78_tau_ops0, s,
                          "rl78-tau-mmio[0]", 0x04);
    memory_region_init_io(&s->mmio[1], OBJECT(s), &rl78_tau_ops1, s,
                          "rl78-tau-mmio[1]", 0x0C);
    memory_region_init_io(&s->mmio[2], OBJECT(s), &rl78_tau_ops2, s,
                          "rl78-tau-mmio[2]", 0x40);
    memory_region_init_io(&s->mmio[3], OBJECT(s), &rl78_tau_ops3, s,
                          "rl78-tau-mmio[3]", 0x02);

    sysbus_init_mmio(sys, &s->mmio[0]);
    sysbus_init_mmio(sys, &s->mmio[1]);
    sysbus_init_mmio(sys, &s->mmio[2]);
    sysbus_init_mmio(sys, &s->mmio[3]);

    qdev_init_gpio_out_named(DEVICE(s), s->irqs, "irq-out",
                             RL78_TAU_CHANNEL_NUM);
    qdev_init_gpio_out_named(DEVICE(s), s->high_irqs, "irq-out-high",
                             RL78_TAU_CHANNEL_NUM);

    for (int i = 0; i < RL78_TAU_CHANNEL_NUM; i++) {
        timer_init_ns(&s->channel[i].timer, QEMU_CLOCK_VIRTUAL,
                      rl78_tau_txend_callbacks[i], s);
        timer_init_ns(&s->channel[i].high_timer, QEMU_CLOCK_VIRTUAL,
                      rl78_tau_txend_callbacks_high[i], s);
    }
}

static void rl78_tau_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    RL78TAUClass *sc    = RL78_TAU_CLASS(klass);

    resettable_class_set_parent_phases(rc, NULL, rl78_tau_reset_hold, NULL,
                                       &sc->parent_phases);
}

static const TypeInfo rl78_tau_info = {
    .name          = TYPE_RL78_TAU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RL78TAUState),
    .instance_init = rl78_tau_init,
    .class_size    = sizeof(RL78TAUClass),
    .class_init    = rl78_tau_class_init,
};

static void rl78_tau_register_types(void)
{
    type_register_static(&rl78_tau_info);
}

type_init(rl78_tau_register_types)
