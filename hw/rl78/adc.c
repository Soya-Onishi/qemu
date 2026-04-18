#include "qemu/osdep.h"
#include "qemu/compiler.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/core/irq.h"
#include "hw/core/registerfields.h"
#include "hw/core/clock.h"
#include "hw/core/qdev.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/resettable.h"
#include "hw/core/qdev-properties.h"
#include "hw/rl78/adc.h"

struct RL78ADCClass {
    /* private */
    SysBusDeviceClass parent_class;

    /* public */
    ResettablePhases parent_phases;
};
typedef struct RL78ADCClass RL78ADCClass;

DECLARE_CLASS_CHECKERS(RL78ADCClass, RL78_ADC, TYPE_RL78_ADC)

REG8(ADM0, 0x00)
FIELD(ADM0, ADCE, 0, 1)
FIELD(ADM0, LV, 1, 2)
FIELD(ADM0, FR, 3, 3)
FIELD(ADM0, ADMD, 6, 1)
FIELD(ADM0, ADCS, 7, 1)

REG8(ADM1, 0x02)
FIELD(ADM1, ADTRS, 0, 3)
FIELD(ADM1, ADLSP, 3, 1)
FIELD(ADM1, ADSCM, 5, 1)
FIELD(ADM1, ADTMD, 6, 2)

REG8(ADM2, 0x00)
FIELD(ADM2, ADTYP, 0, 2)
FIELD(ADM2, AWC, 2, 1)
FIELD(ADM2, ADRCK, 3, 1)
FIELD(ADM2, ADREFM, 5, 1)
FIELD(ADM2, ADREFP, 6, 2)

REG8(ADS, 0x01)
FIELD(ADS, ADS, 0, 5)
FIELD(ADS, ADISS, 7, 1)

REG8(ADUL, 0x01)
REG8(ADLL, 0x02)

REG8(ADTES, 0x03)
FIELD(ADTES, ADTES, 0, 2)

static uint8_t rl78_adc_clock_divider(RL78ADCState *s)
{
    if (s->is_lowspeed_clock) {
        switch (s->fr) {
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "invalid value for FR in ADLSP=1 state.\n");
            return 4;
        case 3:
            return 4;
        case 4:
            return 2;
        case 5:
            return 1;
        }
    } else {
        switch (s->fr) {
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "invalid value for FR in ADLSP=0 state.\n");
            return 32;
        case 0:
            return 32;
        case 1:
            return 16;
        case 2:
            return 8;
        case 3:
            return 4;
        case 4:
            return 2;
        case 5:
            return 1;
        }
    }
}

static uint32_t rl78_adc_clock_wakeup_cycles(RL78ADCState *s)
{
    // for no wait mode
    if (s->is_comparator_enabled) {
        return 1;
    }

    // for wait mode
    if (s->is_lowspeed_clock) {
        switch (s->fr) {
        default:
        case 3:
        case 4:
            return 4;
        case 5:
            return 6;
        }
    } else {
        switch (s->fr) {
        default:
        case 0:
        case 1:
            return 4;
        case 2:
            return 6;
        case 3:
            return 10;
        case 4:
            return 18;
        case 5:
            return 34;
        }
    }
}

static uint32_t rl78_adc_clock_interrupt_delay_cycles(RL78ADCState *s)
{
    // for no wait mode
    if (s->is_comparator_enabled) {
        return 1;
    }

    if (s->operation_mode == RL78_ADC_OPERATION_MODE_CONTINUOUS) {
        return 1;
    }

    return 4;
}

static uint32_t rl78_adc_clock_cycles(RL78ADCState *s)
{
    uint32_t cycles = 0;
    switch (s->lv) {
    case 0:
        cycles = 64;
        break;
    case 1:
        cycles = 181;
        break;
    case 2:
        cycles = 80;
        break;
    case 3:
        cycles = 107;
        break;
    }

    return cycles;
}

static void rl78_adc_start_adc(RL78ADCState *s)
{
    const uint8_t divider        = rl78_adc_clock_divider(s);
    const uint32_t cycles        = rl78_adc_clock_cycles(s);
    const uint32_t wakeup_cycles = rl78_adc_clock_wakeup_cycles(s);
    const uint32_t interrupt_delay_cycles =
        rl78_adc_clock_interrupt_delay_cycles(s);
    const uint32_t total_cycles =
        cycles + wakeup_cycles + interrupt_delay_cycles;
    const double clock_duration = 1.0 / clock_get_hz(s->inclk);
    const double adc_duration   = clock_duration * divider * total_cycles;
    const uint64_t duration_ns  = (uint64_t)(adc_duration * 1000 * 1000 * 1000);

    timer_mod(&s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + duration_ns);
    s->scan_index = 0;
}

static void rl78_adc_stop_adc(RL78ADCState *s) { timer_del(&s->timer); }

static void rl78_adc_update_adm0(RL78ADCState *s, uint8_t value)
{
    // TODO: assert if updating ADMD, FR*, LV* bits in the state of ADCS=1 or
    // ADCE=1.
    // TODO: assert value for combinations of FR* bits and ADLSP bit

    const uint8_t convert_mode = FIELD_EX8(value, ADM0, ADMD);
    const uint8_t adcs         = FIELD_EX8(value, ADM0, ADCS);
    const uint8_t adce         = FIELD_EX8(value, ADM0, ADCE);
    const uint8_t lv           = FIELD_EX8(value, ADM0, LV);
    const uint8_t fr           = FIELD_EX8(value, ADM0, FR);

    const bool is_sleeping =
        !s->is_conversion_running && !s->is_comparator_enabled;

    if (!is_sleeping && convert_mode != s->convert_mode) {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "Changing ADMD must be in the state of ADCS=0 and ADCE=0\n");
    }

    if (!is_sleeping && fr != s->fr) {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "Changing FR must be in the state of ADCS=0 and ADCE=0\n");
    }

    if (!is_sleeping && lv != s->lv) {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "Changing LV must be in the state of ADCS=0 and ADCE=0\n");
    }

    if (s->is_conversion_running && s->is_comparator_enabled && adcs == 1 &&
        adce == 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "Changing from ADCS=1 and ADCE=1 to "
                                       "ADCS=1 and ADCE=0 is forbidden\n");
    }

    if (is_sleeping && adcs == 1 && adce == 1) {
        qemu_log_mask(LOG_GUEST_ERROR, "Changing from ADCS=0 and ADCE=0 to "
                                       "ADCS=1 and ADCE=1 is forbidden\n");
    }

    // No delays are required when updating ADCS=0 and ADCE=0 to ADCS=1 and
    // ADCE=1 for simplicity No delays are required when updating ADMD, FR, LV
    // bits for simplicity

    s->convert_mode          = convert_mode;
    s->is_conversion_running = adcs;
    s->is_comparator_enabled = adce;
    s->lv                    = lv;
    s->fr                    = fr;

    // TODO: check actual MCU behavior when using 1bit manipulation.
    //       If ADC resumes when changing in actual MCU, this emulation is
    //       incorrect because QEMU 1bit manipulation is implemented by a byte
    //       size RMW operation.
    if (s->is_conversion_running) {
        rl78_adc_start_adc(s);
    } else {
        rl78_adc_stop_adc(s);
    }
}

static void rl78_adc_update_adm1(RL78ADCState *s, uint8_t value)
{
    if (s->is_conversion_running || s->is_comparator_enabled) {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "Changing ADM1 must be in the state of ADCS=0 and ADCE=0\n");
    }

    const uint8_t adtmd = FIELD_EX8(value, ADM1, ADTMD);
    const uint8_t adlsp = FIELD_EX8(value, ADM1, ADLSP);
    const uint8_t adscm = FIELD_EX8(value, ADM1, ADSCM);
    const uint8_t adtrs = FIELD_EX8(value, ADM1, ADTRS);

    switch (adtmd) {
    case 0:
    case 1:
        s->trigger_mode = RL78_ADC_TRIGGER_MODE_SOFTWARE;
        break;
    case 2:
        s->trigger_mode = RL78_ADC_TRIGGER_MODE_HARDWARE_NOWAIT;
        break;
    case 3:
        s->trigger_mode = RL78_ADC_TRIGGER_MODE_HARDWARE_WAIT;
        break;
    }
    s->is_lowspeed_clock = adlsp;

    switch (adscm) {
    case 0:
        s->operation_mode = RL78_ADC_OPERATION_MODE_CONTINUOUS;
        break;
    case 1:
        s->operation_mode = RL78_ADC_OPERATION_MODE_ONESHOT;
        break;
    }

    switch (adtrs) {
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "invalid value for ADTRS.\n");
        /* fall-through */
    case 0:
        s->hw_trigger = RL78_ADC_HW_TRIGGER_TAU;
        break;
    case 2:
        s->hw_trigger = RL78_ADC_HW_TRIGGER_RTC;
        break;
    case 3:
        s->hw_trigger = RL78_ADC_HW_TRIGGER_ITL32;
        break;
    case 4:
        s->hw_trigger = RL78_ADC_HW_TRIGGER_ELCL;
        break;
    }
}

static void rl78_adc_update_adm2(RL78ADCState *s, uint8_t value)
{
    if (s->is_conversion_running || s->is_comparator_enabled) {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "Changing ADM2 must be in the state of ADCS=0 and ADCE=0\n");
    }

    if (value & 0x10) {
        qemu_log_mask(LOG_GUEST_ERROR, "ADM2 register, bit4 must be 0\n");
    }

    const uint8_t adrefp = FIELD_EX8(value, ADM2, ADREFP);
    const uint8_t adrefm = FIELD_EX8(value, ADM2, ADREFM);
    const uint8_t adrck  = FIELD_EX8(value, ADM2, ADRCK);
    const uint8_t awc    = FIELD_EX8(value, ADM2, AWC);
    const uint8_t adtyp  = FIELD_EX8(value, ADM2, ADTYP);

    if (s->reference_voltage != RL78_ADC_REFERENCE_VOLTAGE_DISCHARGE &&
        adrefp == RL78_ADC_REFERENCE_VOLTAGE_INTERNAL) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Changing ADREFP to internal standard voltage is valid "
                      "only when from discharge.\n");
    }

    s->reference_voltage  = adrefp;
    s->reference_gnd      = adrefm;
    s->interrupt_in_range = !adrck;
    s->use_snooze         = awc;

    switch (adtyp) {
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "invalid value for ADTYP.\n");
        /* fall-through */
    case 0:
        s->resolution = RL78_ADC_RESOLUTION_10_BITS;
        break;
    case 1:
        s->resolution = RL78_ADC_RESOLUTION_8_BITS;
        break;
    case 2:
        s->resolution = RL78_ADC_RESOLUTION_12_BITS;
        break;
    }
}

static void rl78_adc_update_adcr(RL78ADCState *s, uint16_t value)
{
    qemu_log_mask(LOG_GUEST_ERROR, "ADCR is readonly\n");
}

static void rl78_adc_update_adcrn(RL78ADCState *s, uint16_t value,
                                  uint8_t select)
{
    qemu_log_mask(LOG_GUEST_ERROR, "ADCR%d is readonly\n", select);
}

static void rl78_adc_update_adcrh(RL78ADCState *s, uint8_t value)
{
    qemu_log_mask(LOG_GUEST_ERROR, "ADCRH is readonly\n");
}

static void rl78_adc_update_adcrnh(RL78ADCState *s, uint8_t value,
                                   uint8_t select)
{
    qemu_log_mask(LOG_GUEST_ERROR, "ADCR%dH is readonly\n", select);
}

static void rl78_adc_update_ads(RL78ADCState *s, uint8_t value)
{
    const uint8_t ads   = FIELD_EX8(value, ADS, ADS);
    const uint8_t adiss = FIELD_EX8(value, ADS, ADISS);

    RL78ADCInputSource input;
    if (adiss == 0) {
        switch (ads) {
        case 0:
            input = RL78_ADC_INPUT_SOURCE_ANI0;
            break;
        case 1:
            input = RL78_ADC_INPUT_SOURCE_ANI1;
            break;
        case 2:
            input = RL78_ADC_INPUT_SOURCE_ANI2;
            break;
        case 3:
            input = RL78_ADC_INPUT_SOURCE_ANI3;
            break;
        case 4:
            input = RL78_ADC_INPUT_SOURCE_ANI4;
            break;
        case 5:
            input = RL78_ADC_INPUT_SOURCE_ANI5;
            break;
        case 6:
            input = RL78_ADC_INPUT_SOURCE_ANI6;
            break;
        case 7:
            input = RL78_ADC_INPUT_SOURCE_ANI7;
            break;
        case 8:
            input = RL78_ADC_INPUT_SOURCE_ANI8;
            break;
        case 9:
            input = RL78_ADC_INPUT_SOURCE_ANI9;
            break;
        case 10:
            input = RL78_ADC_INPUT_SOURCE_ANI10;
            break;
        case 11:
            input = RL78_ADC_INPUT_SOURCE_ANI11;
            break;
        case 12:
            input = RL78_ADC_INPUT_SOURCE_ANI12;
            break;
        case 13:
            input = RL78_ADC_INPUT_SOURCE_ANI13;
            break;
        case 14:
            input = RL78_ADC_INPUT_SOURCE_ANI14;
            break;
        case 16:
            input = RL78_ADC_INPUT_SOURCE_ANI16;
            break;
        case 17:
            input = RL78_ADC_INPUT_SOURCE_ANI17;
            break;
        case 18:
            input = RL78_ADC_INPUT_SOURCE_ANI18;
            break;
        case 19:
            input = RL78_ADC_INPUT_SOURCE_ANI19;
            break;
        case 20:
            input = RL78_ADC_INPUT_SOURCE_ANI20;
            break;
        case 21:
            input = RL78_ADC_INPUT_SOURCE_ANI21;
            break;
        case 22:
            input = RL78_ADC_INPUT_SOURCE_ANI22;
            break;
        case 23:
            input = RL78_ADC_INPUT_SOURCE_ANI23;
            break;
        case 24:
            input = RL78_ADC_INPUT_SOURCE_ANI24;
            break;
        case 25:
            input = RL78_ADC_INPUT_SOURCE_ANI25;
            break;
        case 26:
            input = RL78_ADC_INPUT_SOURCE_ANI26;
            break;
        case 30:
            input = RL78_ADC_INPUT_SOURCE_TSCAP;
            break;
        default:
            input = RL78_ADC_INPUT_SOURCE_ANI0;
            break;
        }
    } else {
        switch (ads) {
        case 0:
            input = RL78_ADC_INPUT_SOURCE_SENSOR;
            break;
        case 1:
            input = RL78_ADC_INPUT_SOURCE_INTERNAL;
            break;
        default:
            input = RL78_ADC_INPUT_SOURCE_SENSOR;
            break;
        }
    }

    if (value & 0x60) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ADS register, bit5 and bit6 must be 0\n");
    }

    const bool old_adiss = RL78_ADC_INPUT_SOURCE_SENSOR <= s->input_source &&
                           s->input_source <= RL78_ADC_INPUT_SOURCE_INTERNAL;
    if ((s->is_conversion_running || s->is_comparator_enabled) &&
        old_adiss != adiss) {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "Changing ADISS must be in the state of ADCS=0 and ADCE=0\n");
    }

    if (s->reference_voltage == RL78_ADC_REFERENCE_VOLTAGE_PORT &&
        input == RL78_ADC_INPUT_SOURCE_ANI0) {
        qemu_log_mask(LOG_GUEST_ERROR, "ANI0 is not allowed when ADREFP=1\n");
    }

    if (s->reference_gnd == RL78_ADC_REFERENCE_GND_PORT &&
        input == RL78_ADC_INPUT_SOURCE_ANI1) {
        qemu_log_mask(LOG_GUEST_ERROR, "ANI1 is not allowed when ADREFM=1\n");
    }

    if (adiss == 1 &&
        s->reference_voltage == RL78_ADC_REFERENCE_VOLTAGE_INTERNAL) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ADISS=1 is not allowed when ADREFP=2\n");
    }

    // TODO: ADISS=1 is forbidden when transitioning from STOP mode to HALT
    // mode.
    // TODO: When ADISS=1, hardware trigger wait mode with oneshot operation
    // mode is not allowed.
    // TODO: When ADISS=1, software trigger wait mode with oneshot operation
    // mode is not allowed.

    s->input_source = input;

    // If ADC is already running, restarting ADC by selected input.
    if (s->is_conversion_running) {
        rl78_adc_start_adc(s);
    }
}

static void rl78_adc_update_adul(RL78ADCState *s, uint8_t value)
{
    if (s->is_conversion_running || s->is_comparator_enabled) {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "Changing ADUL must be in the state of ADCS=0 and ADCE=0\n");
    }

    if (value <= s->adll) {
        qemu_log_mask(LOG_GUEST_ERROR, "ADUL must be greater than ADLL\n");
    }

    s->adul = value;
}

static void rl78_adc_update_adll(RL78ADCState *s, uint8_t value)
{
    if (s->is_conversion_running || s->is_comparator_enabled) {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "Changing ADLL must be in the state of ADCS=0 and ADCE=0\n");
    }

    if (value >= s->adul) {
        qemu_log_mask(LOG_GUEST_ERROR, "ADLL must be less than ADUL\n");
    }

    s->adll = value;
}

static void rl78_adc_update_adtes(RL78ADCState *s, uint8_t value)
{
    if (value & 0xFC) {
        qemu_log_mask(LOG_GUEST_ERROR, "ADTES register, bit7-2 must be 0\n");
    }

    RL78ADCTestTarget target;
    switch (FIELD_EX8(value, ADTES, ADTES)) {
    case 0:
        target = RL78_ADC_TEST_TARGET_NORMAL;
        break;
    case 2:
        target = RL78_ADC_TEST_TARGET_GNDREF;
        break;
    case 3:
        target = RL78_ADC_TEST_TARGET_VDDREF;
        break;
    default:
        target = RL78_ADC_TEST_TARGET_NORMAL;
        break;
    }
    // TODO: error log when invalid value;

    s->test_target = target;
}

static uint8_t rl78_adc_read_adm0(RL78ADCState *s)
{
    uint8_t value = 0;

    value = FIELD_DP8(value, ADM0, ADCS, s->is_conversion_running);
    value = FIELD_DP8(value, ADM0, ADCE, s->is_comparator_enabled);
    value = FIELD_DP8(value, ADM0, LV, s->lv);
    value = FIELD_DP8(value, ADM0, FR, s->fr);
    switch (s->convert_mode) {
    case RL78_ADC_CONVERT_MODE_SELECT:
        value = FIELD_DP8(value, ADM0, ADMD, 0);
        break;
    case RL78_ADC_CONVERT_MODE_SCAN:
        value = FIELD_DP8(value, ADM0, ADMD, 1);
        break;
    }

    return value;
}

static uint8_t rl78_adc_read_adm1(RL78ADCState *s)
{
    uint8_t value = 0;

    value = FIELD_DP8(value, ADM1, ADLSP, s->is_lowspeed_clock);
    switch (s->trigger_mode) {
    case RL78_ADC_TRIGGER_MODE_SOFTWARE:
        value = FIELD_DP8(value, ADM1, ADTMD, 0);
        break;
    case RL78_ADC_TRIGGER_MODE_HARDWARE_NOWAIT:
        value = FIELD_DP8(value, ADM1, ADTMD, 2);
        break;
    case RL78_ADC_TRIGGER_MODE_HARDWARE_WAIT:
        value = FIELD_DP8(value, ADM1, ADTMD, 3);
        break;
    }

    switch (s->operation_mode) {
    case RL78_ADC_OPERATION_MODE_CONTINUOUS:
        value = FIELD_DP8(value, ADM1, ADSCM, 0);
        break;
    case RL78_ADC_OPERATION_MODE_ONESHOT:
        value = FIELD_DP8(value, ADM1, ADSCM, 1);
        break;
    }

    switch (s->hw_trigger) {
    case RL78_ADC_HW_TRIGGER_TAU:
        value = FIELD_DP8(value, ADM1, ADTRS, 0);
        break;
    case RL78_ADC_HW_TRIGGER_RTC:
        value = FIELD_DP8(value, ADM1, ADTRS, 2);
        break;
    case RL78_ADC_HW_TRIGGER_ITL32:
        value = FIELD_DP8(value, ADM1, ADTRS, 3);
        break;
    case RL78_ADC_HW_TRIGGER_ELCL:
        value = FIELD_DP8(value, ADM1, ADTRS, 4);
        break;
    }

    return value;
}

static uint8_t rl78_adc_read_adm2(RL78ADCState *s)
{
    uint8_t value = 0;

    switch (s->reference_voltage) {
    case RL78_ADC_REFERENCE_VOLTAGE_VDD:
        value = FIELD_DP8(value, ADM2, ADREFP, 0);
        break;
    case RL78_ADC_REFERENCE_VOLTAGE_PORT:
        value = FIELD_DP8(value, ADM2, ADREFP, 1);
        break;
    case RL78_ADC_REFERENCE_VOLTAGE_INTERNAL:
        value = FIELD_DP8(value, ADM2, ADREFP, 2);
        break;
    case RL78_ADC_REFERENCE_VOLTAGE_DISCHARGE:
        value = FIELD_DP8(value, ADM2, ADREFP, 3);
        break;
    }

    switch (s->reference_gnd) {
    case RL78_ADC_REFERENCE_GND_VDD:
        value = FIELD_DP8(value, ADM2, ADREFM, 0);
        break;
    case RL78_ADC_REFERENCE_GND_PORT:
        value = FIELD_DP8(value, ADM2, ADREFM, 1);
        break;
    }

    switch (s->resolution) {
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "invalid value for ADTYP.\n");
        break;
    case RL78_ADC_RESOLUTION_8_BITS:
        value = FIELD_DP8(value, ADM2, ADTYP, 1);
        break;
    case RL78_ADC_RESOLUTION_10_BITS:
        value = FIELD_DP8(value, ADM2, ADTYP, 0);
        break;
    case RL78_ADC_RESOLUTION_12_BITS:
        value = FIELD_DP8(value, ADM2, ADTYP, 2);
        break;
    }

    value = FIELD_DP8(value, ADM2, ADRCK, s->interrupt_in_range);
    value = FIELD_DP8(value, ADM2, AWC, s->use_snooze);

    return value;
}

static uint16_t rl78_adc_read_adcrn(RL78ADCState *s, uint8_t select)
{
    return s->scan_adcr[select];
}

static uint16_t rl78_adc_read_adcr(RL78ADCState *s) { return s->adcr; }

static uint8_t rl78_adc_read_adcrnh(RL78ADCState *s, uint8_t select)
{
    if (s->resolution == RL78_ADC_RESOLUTION_12_BITS) {
        return (s->scan_adcr[select] >> 4 & 0x00FF);
    } else {
        return (s->scan_adcr[select] >> 8 & 0x00FF);
    }
}

static uint8_t rl78_adc_read_adcrh(RL78ADCState *s)
{
    if (s->resolution == RL78_ADC_RESOLUTION_12_BITS) {
        return s->adcr >> 4 & 0x00FF;
    } else {
        return s->adcr >> 8 & 0x00FF;
    }
}

static uint8_t rl78_adc_read_ads(RL78ADCState *s)
{
    const uint8_t ads_values[] = {
        [RL78_ADC_INPUT_SOURCE_ANI0]     = FIELD_DP8(0, ADS, ADS, 0),
        [RL78_ADC_INPUT_SOURCE_ANI1]     = FIELD_DP8(0, ADS, ADS, 1),
        [RL78_ADC_INPUT_SOURCE_ANI2]     = FIELD_DP8(0, ADS, ADS, 2),
        [RL78_ADC_INPUT_SOURCE_ANI3]     = FIELD_DP8(0, ADS, ADS, 3),
        [RL78_ADC_INPUT_SOURCE_ANI4]     = FIELD_DP8(0, ADS, ADS, 4),
        [RL78_ADC_INPUT_SOURCE_ANI5]     = FIELD_DP8(0, ADS, ADS, 5),
        [RL78_ADC_INPUT_SOURCE_ANI6]     = FIELD_DP8(0, ADS, ADS, 6),
        [RL78_ADC_INPUT_SOURCE_ANI7]     = FIELD_DP8(0, ADS, ADS, 7),
        [RL78_ADC_INPUT_SOURCE_ANI8]     = FIELD_DP8(0, ADS, ADS, 8),
        [RL78_ADC_INPUT_SOURCE_ANI9]     = FIELD_DP8(0, ADS, ADS, 9),
        [RL78_ADC_INPUT_SOURCE_ANI10]    = FIELD_DP8(0, ADS, ADS, 10),
        [RL78_ADC_INPUT_SOURCE_ANI11]    = FIELD_DP8(0, ADS, ADS, 11),
        [RL78_ADC_INPUT_SOURCE_ANI12]    = FIELD_DP8(0, ADS, ADS, 12),
        [RL78_ADC_INPUT_SOURCE_ANI13]    = FIELD_DP8(0, ADS, ADS, 13),
        [RL78_ADC_INPUT_SOURCE_ANI14]    = FIELD_DP8(0, ADS, ADS, 14),
        [RL78_ADC_INPUT_SOURCE_ANI16]    = FIELD_DP8(0, ADS, ADS, 16),
        [RL78_ADC_INPUT_SOURCE_ANI17]    = FIELD_DP8(0, ADS, ADS, 17),
        [RL78_ADC_INPUT_SOURCE_ANI18]    = FIELD_DP8(0, ADS, ADS, 18),
        [RL78_ADC_INPUT_SOURCE_ANI19]    = FIELD_DP8(0, ADS, ADS, 19),
        [RL78_ADC_INPUT_SOURCE_ANI20]    = FIELD_DP8(0, ADS, ADS, 20),
        [RL78_ADC_INPUT_SOURCE_ANI21]    = FIELD_DP8(0, ADS, ADS, 21),
        [RL78_ADC_INPUT_SOURCE_ANI22]    = FIELD_DP8(0, ADS, ADS, 22),
        [RL78_ADC_INPUT_SOURCE_ANI23]    = FIELD_DP8(0, ADS, ADS, 23),
        [RL78_ADC_INPUT_SOURCE_ANI24]    = FIELD_DP8(0, ADS, ADS, 24),
        [RL78_ADC_INPUT_SOURCE_ANI25]    = FIELD_DP8(0, ADS, ADS, 25),
        [RL78_ADC_INPUT_SOURCE_ANI26]    = FIELD_DP8(0, ADS, ADS, 26),
        [RL78_ADC_INPUT_SOURCE_TSCAP]    = FIELD_DP8(0, ADS, ADS, 30),
        [RL78_ADC_INPUT_SOURCE_SENSOR]   = FIELD_DP8(0, ADS, ADS, 0) | 0x80,
        [RL78_ADC_INPUT_SOURCE_INTERNAL] = FIELD_DP8(0, ADS, ADS, 1) | 0x80,
    };

    if (s->input_source >= ARRAY_SIZE(ads_values)) {
        return FIELD_DP8(0, ADS, ADS, 0);
    }

    return ads_values[s->input_source];
}

static uint8_t rl78_adc_read_adul(RL78ADCState *s) { return s->adul; }

static uint8_t rl78_adc_read_adll(RL78ADCState *s) { return s->adll; }

static uint8_t rl78_adc_read_adtes(RL78ADCState *s)
{
    uint8_t value = 0;
    switch (s->test_target) {
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "invalid value for ADTES.\n");
        break;
    case RL78_ADC_TEST_TARGET_NORMAL:
        value = FIELD_DP8(value, ADTES, ADTES, 0);
        break;
    case RL78_ADC_TEST_TARGET_VDDREF:
        value = FIELD_DP8(value, ADTES, ADTES, 3);
        break;
    case RL78_ADC_TEST_TARGET_GNDREF:
        value = FIELD_DP8(value, ADTES, ADTES, 2);
        break;
    }

    return value;
}

static void rl78_adc_write0(void *opaque, hwaddr offset, uint64_t data,
                            unsigned size)
{
    RL78ADCState *s = RL78_ADC(opaque);

    switch (offset) {
    case 0x00:
        if (size == 1) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ADCR register is only accessible by 16bit access\n");
        }
        rl78_adc_update_adcr(s, (uint16_t)data);
        break;
    case 0x01:
        rl78_adc_update_adcrh(s, (uint8_t)data);
        break;
    }
}

static void rl78_adc_write1(void *opaque, hwaddr offset, uint64_t data,
                            unsigned size)
{
    RL78ADCState *s = RL78_ADC(opaque);

    switch (offset) {
    case 0x00:
        rl78_adc_update_adm0(s, (uint8_t)data);
        break;
    case 0x01:
        rl78_adc_update_ads(s, (uint8_t)data);
        break;
    case 0x02:
        rl78_adc_update_adm1(s, (uint8_t)data);
        break;
    }
}

static void rl78_adc_write2(void *opaque, hwaddr offset, uint64_t data,
                            unsigned size)
{
    RL78ADCState *s = RL78_ADC(opaque);

    if (size == 1) {
        switch (offset) {
        case 0x00:
            rl78_adc_update_adm2(s, (uint8_t)data);
            break;
        case 0x01:
            rl78_adc_update_adul(s, (uint8_t)data);
            break;
        case 0x02:
            rl78_adc_update_adll(s, (uint8_t)data);
            break;
        case 0x03:
            rl78_adc_update_adtes(s, (uint8_t)data);
            break;
        case 0x10:
        case 0x12:
        case 0x14:
        case 0x16:
            qemu_log_mask(
                LOG_GUEST_ERROR,
                "ADCRn register is only accessible by 16bit access\n");
            break;
        case 0x11:
        case 0x13:
        case 0x15:
        case 0x17: {
            const uint8_t select = (offset - 0x11) / 2;
            rl78_adc_update_adcrnh(s, (uint8_t)data, select);
            break;
        }
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "Unknown register");
            break;
        }
    } else {
        switch (offset) {
        case 0x00:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ADM2 register is only accessible by 8bit access\n");
            break;
        case 0x01:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ADUL register is only accessible by 8bit access\n");
            break;
        case 0x02:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ADLL register is only accessible by 8bit access\n");
            break;
        case 0x03:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ADTES register is only accessible by 8bit access\n");
            break;
        case 0x10:
        case 0x12:
        case 0x14:
        case 0x16: {
            const uint8_t select = (offset - 0x10) / 2;
            rl78_adc_update_adcrn(s, (uint16_t)data, select);
            break;
        }
        case 0x11:
        case 0x13:
        case 0x15:
        case 0x17:
            qemu_log_mask(
                LOG_GUEST_ERROR,
                "ADCRnH register is only accessible by 8bit access\n");
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "Unknown register");
            break;
        }
    }
}

static uint64_t rl78_adc_read0(void *opaque, hwaddr offset, unsigned size)
{
    RL78ADCState *s = RL78_ADC(opaque);

    switch (offset) {
    case 0x00:
        if (size == 1) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ADCR register is only accessible by 16bit access\n");
            return 0;
        }
        return rl78_adc_read_adcr(s);
    case 0x01:
        return rl78_adc_read_adcrh(s);
    }

    return 0;
}

static uint64_t rl78_adc_read1(void *opaque, hwaddr offset, unsigned size)
{
    RL78ADCState *s = RL78_ADC(opaque);

    switch (offset) {
    case 0x00:
        return rl78_adc_read_adm0(s);
    case 0x01:
        return rl78_adc_read_ads(s);
    case 0x02:
        return rl78_adc_read_adm1(s);
    }

    return 0;
}

static uint64_t rl78_adc_read2(void *opaque, hwaddr offset, unsigned size)
{
    RL78ADCState *s = RL78_ADC(opaque);

    if (size == 1) {
        switch (offset) {
        case 0x00:
            return rl78_adc_read_adm2(s);
        case 0x01:
            return rl78_adc_read_adul(s);
        case 0x02:
            return rl78_adc_read_adll(s);
        case 0x03:
            return rl78_adc_read_adtes(s);
        case 0x10:
        case 0x12:
        case 0x14:
        case 0x16:
            qemu_log_mask(
                LOG_GUEST_ERROR,
                "ADCRn register is only accessible by 16bit access\n");
            return 0;
        case 0x11:
        case 0x13:
        case 0x15:
        case 0x17: {
            const uint8_t select = (offset - 0x11) / 2;
            return rl78_adc_read_adcrnh(s, select);
        }
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "Unknown register");
            return 0;
        }
    } else {
        switch (offset) {
        case 0x00:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ADM2 register is only accessible by 8bit access\n");
            return 0;
        case 0x01:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ADUL register is only accessible by 8bit access\n");
            return 0;
        case 0x02:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ADLL register is only accessible by 8bit access\n");
            return 0;
        case 0x03:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ADTES register is only accessible by 8bit access\n");
            return 0;
        case 0x10:
        case 0x12:
        case 0x14:
        case 0x16: {
            const uint8_t select = (offset - 0x10) / 2;
            return rl78_adc_read_adcrn(s, select);
        }
        case 0x11:
        case 0x13:
        case 0x15:
        case 0x17:
            qemu_log_mask(
                LOG_GUEST_ERROR,
                "ADCRnH register is only accessible by 8bit access\n");
            return 0;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "Unknown register");
            return 0;
        }
    }
}

MemoryRegionOps rl78_adc_mmio_ops0 = {
    .read                  = rl78_adc_read0,
    .write                 = rl78_adc_write0,
    .valid.max_access_size = 2,
    .valid.min_access_size = 1,
    .impl.max_access_size  = 2,
    .impl.min_access_size  = 1,
};

MemoryRegionOps rl78_adc_mmio_ops1 = {
    .read                  = rl78_adc_read1,
    .write                 = rl78_adc_write1,
    .valid.max_access_size = 1,
    .valid.min_access_size = 1,
    .impl.max_access_size  = 1,
    .impl.min_access_size  = 1,
};

MemoryRegionOps rl78_adc_mmio_ops2 = {
    .read                  = rl78_adc_read2,
    .write                 = rl78_adc_write2,
    .valid.max_access_size = 2,
    .valid.min_access_size = 1,
    .impl.max_access_size  = 2,
    .impl.min_access_size  = 1,
};

static uint8_t rl78_adc_fetch_adc_index(const RL78ADCInputSource input,
                                        const uint8_t scan_index)
{
    return (uint8_t)input + scan_index;
}

static uint16_t rl78_adc_fetch_adc_result(RL78ADCState *s,
                                          const RL78ADCInputSource input,
                                          const uint8_t scan_index)
{
    const uint8_t index = rl78_adc_fetch_adc_index(input, scan_index);

    // TODO: support TSCAP, Sensor, Internal.
    switch (input) {
    case RL78_ADC_INPUT_SOURCE_TSCAP:
    case RL78_ADC_INPUT_SOURCE_SENSOR:
    case RL78_ADC_INPUT_SOURCE_INTERNAL:
        return 0;
    default:
        break;
    }

    double voltage = s->adc_results[index];

    // TODO: make selectable reference voltage and GND.
    double adc_result = voltage / 5.0;
    uint16_t adc_result_int;
    uint16_t result   = 0;
    switch (s->resolution) {
    case RL78_ADC_RESOLUTION_8_BITS:
        adc_result_int = (uint16_t)(adc_result * 256);
        adc_result_int = adc_result_int > 255 ? 255 : adc_result_int;
        result = adc_result_int << 8;
        break;
    case RL78_ADC_RESOLUTION_10_BITS:
        adc_result_int = (uint16_t)(adc_result * 1024);
        adc_result_int = adc_result_int > 1023 ? 1023 : adc_result_int;
        result      = adc_result_int << 6;
        break;
    case RL78_ADC_RESOLUTION_12_BITS:
        adc_result_int = (uint16_t)(adc_result * 4096);
        adc_result_int = adc_result_int > 4095 ? 4095 : adc_result_int;
        result      = adc_result_int;
        break;
    }

    qemu_log("[%p] adc_result[%d]: %lf(%lf V), int: %d, result: %d\n", s, index, adc_result, voltage, adc_result_int, result);
    return result;
}

static uint16_t rl78_adc_result_range_threshold(RL78ADCState *s, uint16_t range)
{
    switch(s->resolution) {
        case RL78_ADC_RESOLUTION_8_BITS:
        case RL78_ADC_RESOLUTION_10_BITS:
            return range << 8;
        case RL78_ADC_RESOLUTION_12_BITS:
            return range << 4;
        default:
            return 0;
    }
}

static void rl78_adc_timer_end(void *opaque)
{
    RL78ADCState *s = RL78_ADC(opaque);

    // fetch ADC result from pins
    uint16_t adc_result =
        rl78_adc_fetch_adc_result(s, s->input_source, s->scan_index);

    if (s->scan_index == 0) {
        s->adcr = adc_result;
    }

    const uint16_t adul = rl78_adc_result_range_threshold(s, s->adul);
    const uint16_t adll = rl78_adc_result_range_threshold(s, s->adll);
    const bool is_valid_adcr = s->interrupt_in_range
                                   ? (adll <= s->adcr && s->adcr <= adul)
                                   : (s->adcr < adll || adul <= s->adcr);

    if (is_valid_adcr) {
        s->scan_adcr[s->scan_index] = adc_result;
    }

    uint8_t next_scan_index = s->scan_index;
    if (s->convert_mode == RL78_ADC_CONVERT_MODE_SCAN) {
        next_scan_index += 1;
        next_scan_index %= 4;
    }
    s->scan_index = next_scan_index;

    // scan mode: if all scans are done, raise interrupt.
    // select mode: always raise interrupt (next_scan_index is always 0).
    if (next_scan_index == 0 && is_valid_adcr) {
        qemu_irq_pulse(s->irq);
    }

    // if oneshot mode and all channels are scanned, stop ADC.
    // When select mode, next_scan_index is always 0.
    if (s->operation_mode == RL78_ADC_OPERATION_MODE_ONESHOT &&
        next_scan_index == 0) {
        s->is_conversion_running = false;
        return;
    } else {
        // run next adc for continuous mode and scan mode.
        const uint8_t divider       = rl78_adc_clock_divider(s);
        const uint32_t adc_clocks   = rl78_adc_clock_cycles(s);
        const uint32_t delay_clocks = rl78_adc_clock_interrupt_delay_cycles(s);
        const uint32_t total_clocks = adc_clocks + delay_clocks;
        const double clock_duration = 1.0 / clock_get_hz(s->inclk);
        const double adc_duration   = clock_duration * divider * total_clocks;
        const uint64_t duration_ns =
            (uint64_t)(adc_duration * 1000 * 1000 * 1000);

        timer_mod(&s->timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + duration_ns);
    }
}

static void rl78_adc_reset_hold(Object *obj, ResetType type)
{
    RL78ADCState *s = RL78_ADC(obj);

    // ADM0
    s->is_conversion_running = false;
    s->is_comparator_enabled = false;
    s->convert_mode          = RL78_ADC_CONVERT_MODE_SELECT;
    s->lv                    = 0;
    s->fr                    = 0;

    // ADM1
    s->trigger_mode      = RL78_ADC_TRIGGER_MODE_SOFTWARE;
    s->is_lowspeed_clock = false;
    s->operation_mode    = RL78_ADC_OPERATION_MODE_CONTINUOUS;
    s->hw_trigger        = RL78_ADC_HW_TRIGGER_TAU;

    // ADM2
    s->reference_voltage  = RL78_ADC_REFERENCE_VOLTAGE_VDD;
    s->reference_gnd      = RL78_ADC_REFERENCE_GND_VDD;
    s->interrupt_in_range = false;
    s->use_snooze         = false;
    s->resolution         = RL78_ADC_RESOLUTION_10_BITS;

    s->adcr = 0;
    for (int i = 0; i < ARRAY_SIZE(s->scan_adcr); i++) {
        s->scan_adcr[i] = 0;
    }
    s->scan_index = 0;

    // ADS
    s->input_source = RL78_ADC_INPUT_SOURCE_ANI0;

    s->adul = 0xFF;
    s->adll = 0x00;

    s->test_target = RL78_ADC_TEST_TARGET_NORMAL;

    timer_del(&s->timer);
}

static void property_get_double_ptr(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    double value = *(double *)opaque;
    visit_type_number(v, name, &value, errp);
}

static void property_set_double_ptr(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    double *field = opaque;
    double value;

    if (!visit_type_number(v, name, &value, errp)) {
        return;
    }

    *field = value;
}

static void rl78_adc_notify_adc_result(Notifier *notifier, void *data) {
    IOCReceiver *channel = container_of(notifier, IOCReceiver, notify);
    RL78ADCState *s = channel->opaque;

    s->adc_results[channel->index] = *(double *)data;
}

static void rl78_adc_init(Object *obj)
{
    DeviceState *dev  = DEVICE(obj);
    SysBusDevice *sys = SYS_BUS_DEVICE(obj);
    RL78ADCState *s   = RL78_ADC(obj);

    s->inclk = qdev_init_clock_in(dev, "inclk", NULL, dev, ClockUpdate);

    memory_region_init_io(&s->mmio[0], OBJECT(s), &rl78_adc_mmio_ops0, s,
                          "rl78-adc-mmio", 0x02);
    memory_region_init_io(&s->mmio[1], OBJECT(s), &rl78_adc_mmio_ops1, s,
                          "rl78-adc-mmio", 0x03);
    memory_region_init_io(&s->mmio[2], OBJECT(s), &rl78_adc_mmio_ops2, s,
                          "rl78-adc-mmio", 0x18);

    for (int i = 0; i < ARRAY_SIZE(s->mmio); i++) {
        sysbus_init_mmio(sys, &s->mmio[i]);
    }

    sysbus_init_irq(sys, &s->irq);
    timer_init_ns(&s->timer, QEMU_CLOCK_VIRTUAL, rl78_adc_timer_end, s);

    for (int i = 0; i < ARRAY_SIZE(s->adc_results); i++) { 
        s->adc_results[i] = 0.0;
        s->adc_result_channels[i].index = i;
        s->adc_result_channels[i].opaque = s;
        s->adc_result_channels[i].notify.notify = rl78_adc_notify_adc_result;

        char* name = g_strdup_printf("adc-result[%d]", i);
        object_property_add(obj, name, "double", property_get_double_ptr, property_set_double_ptr, NULL, &s->adc_results[i]);
        register_rx_property(obj, &s->adc_result_channels[i], name);

        g_free(name);
    } 

    qdev_init_gpio_out_named(dev, &s->irq, "irq-out", 1);
}

static void rl78_adc_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    RL78ADCClass *ac    = RL78_ADC_CLASS(klass);

    resettable_class_set_parent_phases(rc, NULL, rl78_adc_reset_hold, NULL,
                                       &ac->parent_phases); 
}

static const TypeInfo rl78_adc_info = {
    .name          = TYPE_RL78_ADC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RL78ADCState),
    .instance_init = rl78_adc_init,
    .class_size    = sizeof(RL78ADCClass),
    .class_init    = rl78_adc_class_init,
};

static void rl78_adc_register_types(void)
{
    type_register_static(&rl78_adc_info);
}

type_init(rl78_adc_register_types)
