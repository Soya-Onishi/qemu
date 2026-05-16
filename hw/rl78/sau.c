#include "qemu/osdep.h"
#include "hw/core/registerfields.h"
#include "hw/core/clock.h"
#include "hw/core/resettable.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/irq.h"
#include "qemu/error-report.h"
#include "qemu/notify.h"
#include "chardev/char.h"
#include "qemu/rcu.h"
#include "qemu/log.h"
#include "qom/object.h"
#include "hw/rl78/sau.h"
#include "hw/rl78/intercomm.h"

struct RL78SAUClass {
    /* private */
    SysBusDeviceClass parent_class;

    /* public */
    ResettablePhases parent_phases;
};
typedef struct RL78SAUClass RL78SAUClass;

DECLARE_CLASS_CHECKERS(RL78SAUClass, RL78_SAU, TYPE_RL78_SAU)

// TODO: implement PER0/PRR0 register control logic
// TODO: Serial is enabled when GPIO pin is high, support it.

REG16(SPS, 0x0026)
FIELD(SPS, PRS0, 0, 4)
FIELD(SPS, PRS1, 4, 4)

REG16(SMR, 0x0010)
FIELD(SMR, MD, 0, 3)
FIELD(SMR, SIS, 6, 1)
FIELD(SMR, STS, 8, 1)
FIELD(SMR, CCS, 14, 1)
FIELD(SMR, CKS, 15, 1)

REG16(SCR, 0x0018)
FIELD(SCR, DLS, 0, 2)
FIELD(SCR, SLC, 4, 2)
FIELD(SCR, DIR, 7, 1)
FIELD(SCR, PTC, 8, 2)
FIELD(SCR, EOC, 10, 1)
FIELD(SCR, CKP, 12, 1)
FIELD(SCR, DAP, 13, 1)
FIELD(SCR, RXE, 14, 1)
FIELD(SCR, TXE, 15, 1)

REG16(SDR, 0x00)

REG16(SIR, 0x08)
FIELD(SIR, OVCT, 0, 1)
FIELD(SIR, PECT, 1, 1)
FIELD(SIR, FECT, 2, 1)

REG16(SSR, 0x00)
FIELD(SSR, OVF, 0, 1)
FIELD(SSR, PEF, 1, 1)
FIELD(SSR, FEF, 2, 1)
FIELD(SSR, BFF, 5, 1)
FIELD(SSR, TSF, 6, 1)

REG16(SS, 0x0022)
FIELD(SS, SS, 0, 4)

REG16(ST, 0x0022)
FIELD(ST, ST, 0, 4)

REG16(SOE, 0x002A)
FIELD(SOE, SOE, 0, 4)

REG16(SO, 0x0028)
FIELD(SO, SO, 0, 4)
FIELD(SO, CKO, 8, 4)

static uint32_t rl78_sau_send_bitlength(RL78SAUChannel *ch)
{
    uint32_t bitlength;
    switch (ch->databits) {
    case RL78_SAU_DATABITS_9:
        bitlength = 9;
        break;
    case RL78_SAU_DATABITS_7:
        bitlength = 7;
        break;
    default:
    case RL78_SAU_DATABITS_8:
        bitlength = 8;
        break;
    }

    // TODO: support SPI/I2C mode
    // Currently, only UART mode is expected.
    if(ch->communication_mode == RL78_SAU_COMMUNICATION_MODE_UART) {
        switch (ch->stopbits) {
        // Startbit + No Stopbit
        case RL78_SAU_UART_STOPBITS_0:
            bitlength += 1;
            break;
        // Startbit + One Stopbit
        default:
        case RL78_SAU_UART_STOPBITS_1:
            bitlength += 2;
            break;
        // Startbit + Two Stopbits
        case RL78_SAU_UART_STOPBITS_2:
            bitlength += 3;
            break;
        }

        switch (ch->parity) {
        // No Parity
        default:
        case 0:
            break;
        // Use Parity(Zero/Odd/Even)
        case 1:
        case 2:
        case 3:
            bitlength += 1;
            break;
        }
    } 

    return bitlength;
}

static void rl78_sau_send_byte(RL78SAUChannel *ch, TransmitPort *txport, qemu_irq irq)
{
    const uint64_t clk_duration_ns  = CLOCK_PERIOD_FROM_HZ(ch->clock.fTCLK_hz);
    const uint64_t bitlength        = rl78_sau_send_bitlength(ch);
    const uint64_t send_duration_ns = (bitlength * clk_duration_ns) >> 32;
    uint16_t txdata;

    switch(ch->databits) {
        default:
        case RL78_SAU_DATABITS_8:
            txdata = ch->data & 0x0FF;
            break;
        case RL78_SAU_DATABITS_7:
            txdata = ch->data & 0x07F;
            break;
        case RL78_SAU_DATABITS_9:
            txdata = ch->data & 0x1FF;
            break;
    }

    // TODO: inspect actual MCU movement when incorrect usage.
    // TODO: Is SOE needed for sending data without clock like UART?
    if (!ch->enabled || !ch->output_enabled) {
        return;
    }

    ch->status.is_busy = true;
    ch->status.is_sdr_dirty = false;

    const uint64_t expire_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + send_duration_ns;
    timer_mod(&ch->interval_timer, expire_time);

    WirePayload payload;
    payload.type = WIRE_PAYLOAD_TYPE_SERIAL;
    payload.serial.type = SERIAL_PACKET_TYPE_UART;
    payload.serial.uart.payload = txdata;
    payload.serial.uart.stopbits = ch->stopbits;

    switch(ch->parity) {
        default:
        case 0:
            payload.serial.uart.parity = UART_PARITY_NONE;
            break;
        case 1:
            qemu_log_mask(LOG_UNIMP, "Zero Parity is not supported.");
            exit(1);
        case 2:
            payload.serial.uart.parity = UART_PARITY_EVEN;
            break;
        case 3:
            payload.serial.uart.parity = UART_PARITY_ODD;
            break;
    }

    transmit_port_payload(txport, &payload);

    // TODO: check SO bit for checking valid initial signal status

    if (ch->tx_inttype == RL78_SAU_TX_INTTYPE_SDR_EMPTY) { 
        // SDR empty interrupt
        qemu_set_irq(irq, 1);
    }
}

static void rl78_sau_update_fTCLK(RL78SAUState *s, uint channel)
{
    RL78SAUChannel *ch = &s->channels[channel];
    const uint64_t clk = clock_get_hz(s->inclk);
    uint64_t ck[ARRAY_SIZE(s->ck_divisor)];

    for(int i = 0; i < ARRAY_SIZE(ck); i++) {
        ck[i] = clk / (1 << s->ck_divisor[i]);
    }
    
    const uint64_t fMCK  = ch->clock.ck_select == 0 ? ck[0] : ck[1];
    const uint64_t fMCK_div = fMCK / (ch->clock.divisor + 1) / 2;
    // TODO: support external clock source
    const uint64_t fTCLK = ch->clock.use_internal_clock ? fMCK_div : 0;

    ch->clock.fTCLK_hz = fTCLK;
}

static void rl78_sau_update_inclk(void *opaque, ClockEvent event)
{
    RL78SAUState *s = RL78_SAU(opaque);

    for (int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        rl78_sau_update_fTCLK(s, ch);
    }
}

static void rl78_sau_update_sps(RL78SAUState *s, uint16_t value)
{
    s->ck_divisor[0] = FIELD_EX16(value, SPS, PRS0);
    s->ck_divisor[1] = FIELD_EX16(value, SPS, PRS1);

    for (int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        rl78_sau_update_fTCLK(s, ch);
    }
}

static void rl78_sau_update_smr(RL78SAUState *s, uint16_t value, uint channel)
{
    RL78SAUChannel *ch = &s->channels[channel];
    const uint16_t mask = R_SMR_CKS_MASK 
                        | R_SMR_CCS_MASK
                        | R_SMR_STS_MASK 
                        | R_SMR_SIS_MASK 
                        | R_SMR_MD_MASK;

    if((value & ~mask) != 0x0020) {
        qemu_log_mask(LOG_GUEST_ERROR, "value: 0x%04X is not match reserved bits.\n", value);
    }

    if (ch->enabled) {
        qemu_log_mask(LOG_GUEST_ERROR, "SMR register is not writable when SE bit = 1.\n");
        return;
    } 

    ch->clock.ck_select = FIELD_EX16(value, SMR, CKS);
    ch->clock.use_internal_clock = FIELD_EX16(value, SMR, CCS) == 0;
    ch->trigger = FIELD_EX16(value, SMR, SIS) == 0 
                ? RL78_SAU_START_TRIGGER_SOFTWARE 
                : RL78_SAU_START_TRIGGER_PORTEDGE;
    // TODO: SIS bit support
    const uint8_t mode = FIELD_EX16(value, SMR, MD) >> 1;
    switch(mode) { 
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%02X is not allowed in SMR.MD1-2 bits. treated as UART.\n", mode);
            /* fall-through */
        case 0:
            ch->communication_mode = RL78_SAU_COMMUNICATION_MODE_UART;
            break;
        case 1:
            ch->communication_mode = RL78_SAU_COMMUNICATION_MODE_SPI;
            break;
        case 2:
            ch->communication_mode = RL78_SAU_COMMUNICATION_MODE_I2C;
            break;
    }

    const uint8_t inttype = FIELD_EX16(value, SMR, MD) & 1;
    ch->tx_inttype = inttype == 0
                   ? RL78_SAU_TX_INTTYPE_TX_DONE
                   : RL78_SAU_TX_INTTYPE_SDR_EMPTY;

    rl78_sau_update_fTCLK(s, channel);
}

static void rl78_sau_update_scr(RL78SAUState *s, uint16_t value, uint channel)
{
    RL78SAUChannel *ch = &s->channels[channel];
    const uint16_t mask = R_SCR_DLS_MASK 
                        | R_SCR_SLC_MASK 
                        | R_SCR_DIR_MASK 
                        | R_SCR_PTC_MASK 
                        | R_SCR_EOC_MASK 
                        | R_SCR_CKP_MASK 
                        | R_SCR_DAP_MASK 
                        | R_SCR_RXE_MASK 
                        | R_SCR_TXE_MASK;

    // TODO: support difference between MCU pin count

    if (ch->enabled) {
        qemu_log_mask(LOG_GUEST_ERROR, "SCR register is not writable when SE bit = 1.\n");
        return;
    }

    if((value & ~mask) != 0x0004) {
        qemu_log_mask(LOG_GUEST_ERROR, "value: 0x%04X is not match reserved bits.\n", value);
    }
    
    const uint8_t databits = FIELD_EX16(value, SCR, DLS);
    switch(databits) {
        case 1: 
            ch->databits = RL78_SAU_DATABITS_9;
            break;
        case 2:
            ch->databits = RL78_SAU_DATABITS_7;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "DLS: %02X is not allowed in SCR.DLS0-1 bits. treated as 8-bit.\n", databits);
            /* fall-through */
        case 3:
            ch->databits = RL78_SAU_DATABITS_8;
            break;
    }

    const uint8_t stopbits = FIELD_EX16(value, SCR, SLC);
    switch(stopbits) {
        case 0:
            ch->stopbits = RL78_SAU_UART_STOPBITS_0;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "SLC: %02X is not allowed in SCR.SLC0-2 bits. treated as 1-stopbit.\n", stopbits);
            /* fall-through */
        case 1:
            ch->stopbits = RL78_SAU_UART_STOPBITS_1;
            break;
        case 2:
            ch->stopbits = RL78_SAU_UART_STOPBITS_2;
            break;
    }

    const uint8_t parity = FIELD_EX16(value, SCR, PTC);
    switch(parity) {
        default:
            error_report("Parity bit is overflow.\n");
            /* fall-through */
        case 0:
            ch->parity = RL78_SAU_UART_PARITY_NONE;
            break;
        case 1:
            ch->parity = RL78_SAU_UART_PARITY_ZERO;
            break;
        case 2:
            ch->parity = RL78_SAU_UART_PARITY_EVEN;
            break;
        case 3:
            ch->parity = RL78_SAU_UART_PARITY_ODD;
            break;
    }

    ch->tx_enabled = FIELD_EX16(value, SCR, TXE) == 1;
    ch->rx_enabled = FIELD_EX16(value, SCR, RXE) == 1;
    ch->sre_enabled = FIELD_EX16(value, SCR, EOC) == 1;
    ch->bitdirection = FIELD_EX16(value, SCR, DIR) == 1 
                     ? RL78_SAU_BIT_DIRECTION_LSB 
                     : RL78_SAU_BIT_DIRECTION_MSB;

    // TODO: DAP and CKP bit support
}

static void rl78_sau_update_sdr(RL78SAUState *s, uint16_t value, uint channel)
{
    RL78SAUChannel *ch = &s->channels[channel];
    // TODO: value assertion for specific register
    // TODO: assert specific value assertion when UART mode and I2C mode

    const bool is_running = ch->enabled;

    // not overwrite divisor setting when communication is running (SE bit = 1)
    if(ch->enabled) {
        ch->data = value & 0x01FF;
    }  

    if (!is_running) {
        ch->clock.divisor = value >> 9;
        rl78_sau_update_fTCLK(s, channel);
    }

    if (ch->status.is_sdr_dirty) {
        // TODO: assertion
        qemu_log_mask(LOG_GUEST_ERROR, "SDR data is overwritten, but not being transmitted.");
        ch->status.has_overflow_error = true;
    }

    if (ch->tx_enabled && ch->tx_inttype == RL78_SAU_TX_INTTYPE_SDR_EMPTY) {
        ch->status.is_sdr_dirty = true;
    }

    if (ch->tx_enabled && !ch->status.is_busy) {
        rl78_sau_send_byte(ch, &s->tx_ports[channel], s->irqs[channel]);
    }
}

static void rl78_sau_update_sir(RL78SAUState *s, uint16_t value, uint channel)
{
    RL78SAUChannel *ch = &s->channels[channel];

    const uint16_t mask = R_SIR_OVCT_MASK | R_SIR_PECT_MASK | R_SIR_FECT_MASK;
    if((value & ~mask) != 0x0000) {
        qemu_log_mask(LOG_GUEST_ERROR, "value: 0x%04X is not match reserved bits.\n", value);
    }

    if(value & R_SIR_OVCT_MASK) { 
        ch->status.has_overflow_error = false;
    }

    if(value & R_SIR_PECT_MASK) { 
        ch->status.has_parity_error = false;
    }

    if(value & R_SIR_FECT_MASK) { 
        ch->status.has_framing_error = false;
    }
}

static void rl78_sau_update_ssr(RL78SAUState *s, uint16_t value, uint channel)
{
    qemu_log_mask(LOG_GUEST_ERROR, "SSR register is readonly.\n");
}

static void rl78_sau_update_ss(RL78SAUState *s, uint16_t value)
{
    // RL78 SS register requires 4 clocks delay to set SS bit = 1
    // after setting SCR register RXE bit = 1
    // However, in QEMU, there is no such restrictions for simplicity.

    // TODO: value assertion

    const uint16_t mask  = R_SS_SS_MASK;
    if((value & ~mask) != 0x0000) {
        qemu_log_mask(LOG_GUEST_ERROR, "value: 0x%04X is not match reserved bits.\n", value);
    }

    for (int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        if (value & (1 << ch)) {
            RL78SAUChannel *c = &s->channels[ch];

            c->enabled = true;
            c->status.is_busy = false;
            c->status.is_sdr_dirty = false;

            /**
             * TODO: inspect actual MCU movement
             *       if resetting SS bit = 1 when TX is running.
             */
            timer_del(&c->interval_timer);
        }
    }
}

static void rl78_sau_update_st(RL78SAUState *s, uint16_t value)
{
    // TODO: value assertion

    const uint16_t mask  = R_ST_ST_MASK;
    if((value & ~mask) != 0x0000) {
        qemu_log_mask(LOG_GUEST_ERROR, "value: 0x%04X is not match reserved bits.\n", value);
    }

    for (int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        if (value & (1 << ch)) {
            RL78SAUChannel *c = &s->channels[ch];

            c->enabled = false;
            c->status.is_busy = false;
            c->status.is_sdr_dirty = false;

            /**
             * TODO: inspect actual MCU movement
             *       if resetting ST bit = 1 when TX is running.
             */
            timer_del(&c->interval_timer);
        }
    }
}

static void rl78_sau_update_se(RL78SAUState *s, uint16_t value)
{
    qemu_log_mask(LOG_GUEST_ERROR, "SE register is readonly.\n");
}

static void rl78_sau_update_soe(RL78SAUState *s, uint16_t value)
{
    // TODO: value assertion

    const uint16_t mask = R_SOE_SOE_MASK;
    if((value & ~mask) != 0x0000) {
        qemu_log_mask(LOG_GUEST_ERROR, "value: 0x%04X is not match reserved bits.\n", value);
    }

    for (int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        bool status = !!(value & (1 << ch));

        s->channels[ch].output_enabled = status;
    }
}

static void rl78_sau_update_so(RL78SAUState *s, uint16_t value)
{
    const uint16_t mask = R_SO_SO_MASK | R_SO_CKO_MASK;
    if((value & ~mask) != 0x0000) {
        qemu_log_mask(LOG_GUEST_ERROR, "value: 0x%04X is not match reserved bits.\n", value);
    }

    for (int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        RL78SAUChannel *c = &s->channels[ch];

        uint8_t lo = (value & 0x00FF) >> 0;
        uint8_t hi = (value & 0xFF00) >> 8;

        c->initial_output = !!(lo & (1 << ch));
        c->initial_clock = !!(hi & (1 << ch));
    }

}

static void rl78_sau_update_sol(RL78SAUState *s, uint16_t value)
{
    // TODO: value assertion
    // TODO: assert when not zero value on non UART mode
    if((value & ~0x0005) != 0x0000) {
        qemu_log_mask(LOG_GUEST_ERROR, "value: 0x%04X is not match reserved bits.\n", value);
    }

    s->channels[0].is_level_inverted = !!(value & 0x0001);
    s->channels[2].is_level_inverted = !!(value & 0x0004);
}

static void rl78_sau_update_ssc(RL78SAUState *s, uint16_t value)
{
    // TODO: support this register

    // Currently, SNOOZE mode serial data receive is not supported.
    // assign zero in force.
    qemu_log_mask(LOG_UNIMP, "SSC register is unimplemented.\n");
}

static void rl78_sau_write0(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    RL78SAUState *s = RL78_SAU(opaque);
    switch (offset) {
    case 0:
        rl78_sau_update_sdr(s, value, 0);
        break;
    case 1:
        rl78_sau_update_sdr(s, value, 1);
        break;
    default:
        // TODO: invalid access assertion
        break;
    }
}

static void rl78_sau_write1(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    RL78SAUState *s = RL78_SAU(opaque);
    switch (offset) {
    case 0:
        rl78_sau_update_sdr(s, value, 2);
        break;
    case 1:
        rl78_sau_update_sdr(s, value, 3);
        break;
    default:
        // TODO: invalid access assertion
        break;
    }
}

static void rl78_sau_write2(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    RL78SAUState *s = RL78_SAU(opaque);
    switch (offset) {
    case 0x00:
    case 0x02:
    case 0x04:
    case 0x06: {
        const uint channel = offset / 2;
        rl78_sau_update_ssr(s, value, channel);
        break;
    }
    case 0x08:
    case 0x0A:
    case 0x0C:
    case 0x0E: {
        const uint channel = (offset - 8) / 2;
        rl78_sau_update_sir(s, value, channel);
        break;
    }
    case 0x10:
    case 0x12:
    case 0x14:
    case 0x16: {
        const uint channel = (offset - 0x10) / 2;
        rl78_sau_update_smr(s, value, channel);
        break;
    }
    case 0x18:
    case 0x1A:
    case 0x1C:
    case 0x1E: {
        const uint channel = (offset - 0x18) / 2;
        rl78_sau_update_scr(s, value, channel);
        break;
    }
    case 0x20:
        rl78_sau_update_se(s, value);
        break;
    case 0x22:
        rl78_sau_update_ss(s, value);
        break;
    case 0x24:
        rl78_sau_update_st(s, value);
        break;
    case 0x26:
        rl78_sau_update_sps(s, value);
        break;
    case 0x28:
        rl78_sau_update_so(s, value);
        break;
    case 0x2A:
        rl78_sau_update_soe(s, value);
        break;
    case 0x34:
        rl78_sau_update_sol(s, value);
        break;
    case 0x38:
        rl78_sau_update_ssc(s, value);
        break;
    default:
        // TODO: invalid access assertion
        break;
    }
}

static uint16_t rl78_sau_read_sps(RL78SAUState *s) { 
    const uint16_t prs0 = (s->ck_divisor[0] & 0x0F) << 0;
    const uint16_t prs1 = (s->ck_divisor[1] & 0x0F) << 4;

    return prs0 | prs1;
}

static uint16_t rl78_sau_read_smr(RL78SAUState *s, uint channel)
{
    uint16_t smr = 0x0020;
    RL78SAUChannel *ch = &s->channels[channel];

    smr = FIELD_DP16(smr, SMR, CKS, ch->clock.ck_select);
    smr = FIELD_DP16(smr, SMR, CCS, ch->clock.use_internal_clock ? 0 : 1);
    smr = FIELD_DP16(smr, SMR, STS, ch->trigger == RL78_SAU_START_TRIGGER_SOFTWARE ? 0 : 1);
    // TODO: support SIS bit
    smr = FIELD_DP16(smr, SMR, SIS, 0);

    switch(ch->communication_mode) {
        default:
            error_report("Invalid communication mode: %d, and treated as UART", ch->communication_mode);
            /* fall-through */
        case RL78_SAU_COMMUNICATION_MODE_UART:
            smr = FIELD_DP16(smr, SMR, MD, 1 << 1);
            break;
        case RL78_SAU_COMMUNICATION_MODE_SPI:
            smr = FIELD_DP16(smr, SMR, MD, 0 << 1);
            break;
        case RL78_SAU_COMMUNICATION_MODE_I2C:
            smr = FIELD_DP16(smr, SMR, MD, 2 << 1);
            break;
    }
    smr |= ch->tx_inttype == RL78_SAU_TX_INTTYPE_TX_DONE ? 0 : 1;

    return smr;
}

static uint16_t rl78_sau_read_scr(RL78SAUState *s, uint channel)
{
    RL78SAUChannel *ch = &s->channels[channel];
    uint16_t scr = 0x0004;

    scr = FIELD_DP16(scr, SCR, TXE, ch->tx_enabled ? 1 : 0);
    scr = FIELD_DP16(scr, SCR, RXE, ch->rx_enabled ? 1 : 0);
    scr = FIELD_DP16(scr, SCR, DAP, 0 /* TODO: support DAP bit */);
    scr = FIELD_DP16(scr, SCR, CKP, 0 /* TODO: support CKP bit */);
    scr = FIELD_DP16(scr, SCR, EOC, ch->sre_enabled ? 1 : 0);
    switch(ch->parity) {
        case RL78_SAU_UART_PARITY_NONE:
            scr = FIELD_DP16(scr, SCR, PTC, 0);
            break;
        case RL78_SAU_UART_PARITY_ZERO:
            scr = FIELD_DP16(scr, SCR, PTC, 1);
            break;
        case RL78_SAU_UART_PARITY_EVEN:
            scr = FIELD_DP16(scr, SCR, PTC, 2);
            break;
        case RL78_SAU_UART_PARITY_ODD:
            scr = FIELD_DP16(scr, SCR, PTC, 3);
            break;
    }
    scr = FIELD_DP16(scr, SCR, DIR, ch->bitdirection == RL78_SAU_BIT_DIRECTION_LSB ? 1 : 0);
    switch(ch->stopbits) {
        case RL78_SAU_UART_STOPBITS_0:
            scr = FIELD_DP16(scr, SCR, SLC, 0);
            break;
        case RL78_SAU_UART_STOPBITS_1:
            scr = FIELD_DP16(scr, SCR, SLC, 1);
            break;
        case RL78_SAU_UART_STOPBITS_2:
            scr = FIELD_DP16(scr, SCR, SLC, 2);
            break;
    }
    switch(ch->databits) {
        case RL78_SAU_DATABITS_7:
            scr = FIELD_DP16(scr, SCR, DLS, 2);
            break;
        case RL78_SAU_DATABITS_8:
            scr = FIELD_DP16(scr, SCR, DLS, 3);
            break;
        case RL78_SAU_DATABITS_9:
            scr = FIELD_DP16(scr, SCR, DLS, 1);
            break;
    }

    return scr;
}

static uint16_t rl78_sau_read_sdr(RL78SAUState *s, uint channel)
{
    RL78SAUChannel *ch = &s->channels[channel];
    uint16_t sdr = ch->data & 0x1FF;

    if(!ch->enabled) {
        sdr |= (ch->clock.divisor << 9);
    }

    if(ch->enabled && ch->rx_enabled) {
        ch->status.is_sdr_dirty = false;

        if(!g_queue_is_empty(ch->rx_data_queue)) {
            uint16_t* data = g_queue_pop_head(ch->rx_data_queue);

            ch->status.is_sdr_dirty = true;
            ch->data = *data;
            qemu_set_irq(s->irqs[channel], 1);

            g_free(data);
        }
    }

    return sdr;
}

static uint16_t rl78_sau_read_sir(RL78SAUState *s, uint channel)
{
    // reading this register always results in zero, not using SIR register
    return 0;
}

static uint16_t rl78_sau_read_ssr(RL78SAUState *s, uint channel)
{
    // TODO: procedure for updating SSR status by any trigger
    RL78SAUChannel *ch = &s->channels[channel];
    uint16_t ssr = 0x0000;

    ssr = FIELD_DP16(ssr, SSR, OVF, ch->status.has_overflow_error ? 1 : 0);
    ssr = FIELD_DP16(ssr, SSR, PEF, ch->status.has_parity_error ? 1 : 0);
    ssr = FIELD_DP16(ssr, SSR, FEF, ch->status.has_framing_error ? 1 : 0);
    ssr = FIELD_DP16(ssr, SSR, BFF, ch->status.is_sdr_dirty ? 1 : 0);
    ssr = FIELD_DP16(ssr, SSR, TSF, ch->status.is_busy ? 1 : 0);

    return ssr;
}

static uint16_t rl78_sau_read_ss(RL78SAUState *s)
{
    // reading SS register always results in returning zero
    return 0;
}

static uint16_t rl78_sau_read_st(RL78SAUState *s)
{
    // reading ST register always results in returning zero
    return 0;
}

static uint16_t rl78_sau_read_se(RL78SAUState *s) {
    uint16_t se = 0x0000;

    for(int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        se |= (s->channels[ch].enabled ? 1 << ch : 0);
    }

    return se;
}

static uint16_t rl78_sau_read_soe(RL78SAUState *s) {
    uint16_t soe = 0x0000;

    for(int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        soe |= (s->channels[ch].output_enabled ? 1 << ch : 0);
    }

    return soe;
}

static uint16_t rl78_sau_read_so(RL78SAUState *s)
{
    // TODO: support for reflecting output signal bit
    // Currently, SO register just returns the value set by guest code.
    uint16_t so = 0x0000;

    for(int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        so |= (s->channels[ch].initial_output ? 1 << ch : 0);
        so |= (s->channels[ch].initial_clock ? 1 << (ch + 8) : 0);
    }

    return so;
}

static uint16_t rl78_sau_read_sol(RL78SAUState *s) { 
    uint16_t sol = 0x0000;

    sol |= s->channels[0].initial_output ? 1 << 0 : 0;
    sol |= s->channels[2].initial_output ? 1 << 2 : 0;

    return sol;
 }

static uint16_t rl78_sau_read_ssc(RL78SAUState *s) { 
    // TODO: implement SSC register logic     
    qemu_log_mask(LOG_UNIMP, "SSC register is not implemented.\n");
    return 0; 
}

static uint64_t rl78_sau_read0(void *opaque, hwaddr offset, unsigned size)
{
    RL78SAUState *s = RL78_SAU(opaque);
    switch (offset) {
    case 0:
        return rl78_sau_read_sdr(s, 0);
    case 2:
        return rl78_sau_read_sdr(s, 1);
    default:
        // TODO: invalid access assertion
        return 0;
    }
}

static uint64_t rl78_sau_read1(void *opaque, hwaddr offset, unsigned size)
{
    RL78SAUState *s = RL78_SAU(opaque);
    switch (offset) {
    case 0:
        return rl78_sau_read_sdr(s, 2);
    case 2:
        return rl78_sau_read_sdr(s, 3);
    default:
        // TODO: invalid access assertion
        return 0;
    }
}

static uint64_t rl78_sau_read2(void *opaque, hwaddr offset, unsigned size)
{
    RL78SAUState *s = RL78_SAU(opaque);
    switch (offset) {
    case 0x00:
    case 0x02:
    case 0x04:
    case 0x06: {
        const uint channel = (offset - 0x00) / 2;
        return rl78_sau_read_ssr(s, channel);
    }
    case 0x08:
    case 0x0A:
    case 0x0C:
    case 0x0E: {
        const uint channel = (offset - 0x08) / 2;
        return rl78_sau_read_sir(s, channel);
    }
    case 0x10:
    case 0x12:
    case 0x14:
    case 0x16: {
        const uint channel = (offset - 0x10) / 2;
        return rl78_sau_read_smr(s, channel);
    }
    case 0x18:
    case 0x1A:
    case 0x1C:
    case 0x1E: {
        const uint channel = (offset - 0x18) / 2;
        return rl78_sau_read_scr(s, channel);
    }
    case 0x20:
        return rl78_sau_read_se(s);
    case 0x22:
        return rl78_sau_read_ss(s);
    case 0x24:
        return rl78_sau_read_st(s);
    case 0x26:
        return rl78_sau_read_sps(s);
    case 0x28:
        return rl78_sau_read_so(s);
    case 0x2A:
        return rl78_sau_read_soe(s);
    case 0x34:
        return rl78_sau_read_sol(s);
    case 0x38:
        return rl78_sau_read_ssc(s);
    default:
        // TODO: invalid access assertion
        return 0;
    }
}

static const MemoryRegionOps rl78_sau_ops0 = {
    .write                 = rl78_sau_write0,
    .read                  = rl78_sau_read0,
    .valid.max_access_size = 2,
    .valid.min_access_size = 1,
    .impl.max_access_size  = 2,
    .impl.min_access_size  = 1,
};

static const MemoryRegionOps rl78_sau_ops1 = {
    .write                 = rl78_sau_write1,
    .read                  = rl78_sau_read1,
    .valid.max_access_size = 2,
    .valid.min_access_size = 1,
    .impl.max_access_size  = 2,
    .impl.min_access_size  = 1,
};

static const MemoryRegionOps rl78_sau_ops2 = {
    .write                 = rl78_sau_write2,
    .read                  = rl78_sau_read2,
    .valid.max_access_size = 2,
    .valid.min_access_size = 1,
    .impl.max_access_size  = 2,
    .impl.min_access_size  = 1,
};

static void rl78_sau_reset_hold(Object *obj, ResetType type)
{
    RL78SAUState *s = RL78_SAU(obj);

    s->ck_divisor[0] = 0;
    s->ck_divisor[1] = 0;

    for(int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        RL78SAUChannel *c = &s->channels[ch];

        c->clock.use_internal_clock = false;
        c->clock.divisor = 0;
        c->clock.fTCLK_hz = 1;
        c->clock.ck_select = 0;

        c->trigger = RL78_SAU_START_TRIGGER_SOFTWARE;
        c->startbit = RL78_SAU_UART_RX_STARTBIT_FALL;
        c->communication_mode = RL78_SAU_COMMUNICATION_MODE_SPI;
        c->tx_inttype = RL78_SAU_TX_INTTYPE_TX_DONE;

        c->tx_enabled = false;
        c->rx_enabled = false;
        // TODO: support DAP/CKP bits
        c->sre_enabled = false;
        c->parity = RL78_SAU_UART_PARITY_NONE;
        c->stopbits = RL78_SAU_UART_STOPBITS_0;
        c->databits = RL78_SAU_DATABITS_8;
        c->bitdirection = RL78_SAU_BIT_DIRECTION_LSB;

        c->data = 0;

        c->status.is_busy = false;
        c->status.is_sdr_dirty = false;
        c->status.has_framing_error = false;
        c->status.has_parity_error = false;
        c->status.has_overflow_error = false;

        c->enabled = false;
        c->output_enabled = false;
        c->initial_output = false;
        c->initial_clock = false;
        c->is_level_inverted = false;

        timer_del(&c->interval_timer);
        g_queue_clear(c->rx_data_queue);
    } 
}

static void rl78_sau_tx_timer_up(RL78SAUState *s, int channel)
{
    RL78SAUChannel *ch = &s->channels[channel];

    ch->status.is_busy = false; 
    if(ch->tx_enabled && ch->status.is_sdr_dirty)
        rl78_sau_send_byte(ch, &s->tx_ports[channel], s->irqs[channel]);

    if (ch->tx_inttype == RL78_SAU_TX_INTTYPE_TX_DONE) {
        // TX done interrupt
        qemu_set_irq(s->irqs[channel], 1);
    }
}

#define RL78SAU_TX_TIMER_UP_CALLBACK(channel_num)                              \
    static void rl78_sau_tx_timer_up_channel##channel_num(void *opaque)        \
    {                                                                          \
        RL78SAUState *s = RL78_SAU(opaque);                                    \
        rl78_sau_tx_timer_up(s, channel_num);                                  \
    }

RL78SAU_TX_TIMER_UP_CALLBACK(0)
RL78SAU_TX_TIMER_UP_CALLBACK(1)
RL78SAU_TX_TIMER_UP_CALLBACK(2)
RL78SAU_TX_TIMER_UP_CALLBACK(3)

static void rl78_sau_rx_irq(Object* instance, uint64_t index, const void* payload)
{
    RL78SAUState *s = RL78_SAU(instance);
    RL78SAUChannel *ch0 = &s->channels[index];

    const WirePayload *p = (const WirePayload *)payload;
    // TODO: raise SRE interrupt if serial signal format is unmatched
 
    const bool is_uart = ch0->communication_mode == RL78_SAU_COMMUNICATION_MODE_UART;
    if((index & 0x01) && is_uart) {
        qemu_log_mask(LOG_GUEST_ERROR, "UART signal must be received on even channel.\n");
    }

    if(is_uart) {
        index |= 0x01;
    }

    RL78SAUChannel *ch1 = &s->channels[index];
    if(!(ch1->enabled & (1 << index))) { 
        // If not enabled, ignore the received data
        return;
    }

    if(is_uart && ch1->communication_mode != RL78_SAU_COMMUNICATION_MODE_UART) {
        qemu_log_mask(LOG_GUEST_ERROR, "When using UART mode, SMRm%lu and SMRm%lu must be UART", index - 1, index);
        // only report error, and continue processing
    }
    
    if(!ch1->rx_enabled) {
        // If not RX enabled, ignore the received data
        qemu_log_mask(LOG_GUEST_ERROR, "SCRm%lu is not RX enabled.\n", index);
        return;
    } 

    // Actual MCU, TSF bit is asserted when receiving data, 
    // but QEMU receives byte data at once, so TSF bit is not asserted.
    
    const uint16_t rxdata = p->serial.uart.payload;

    if(ch1->status.is_sdr_dirty) {
        uint16_t* data = g_new(uint16_t, 1);
        *data = rxdata;
        g_queue_push_tail(ch1->rx_data_queue, data);
    } else { 
        ch1->status.is_sdr_dirty = true;
        ch1->data = rxdata;
        qemu_set_irq(s->irqs[index], 1);
    }
}

static void rl78_sau_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *d  = SYS_BUS_DEVICE(dev);
    RL78SAUState *s  = RL78_SAU(obj);
    void (*tx_timer_up_callbacks[RL78_SAU_CHANNEL_NUM])(void *) = {
        rl78_sau_tx_timer_up_channel0,
        rl78_sau_tx_timer_up_channel1,
        rl78_sau_tx_timer_up_channel2,
        rl78_sau_tx_timer_up_channel3,
    };

    memory_region_init_io(&s->mmio[0], OBJECT(s), &rl78_sau_ops0, s,
                          "rl78-sau-mmio[0]", 0x04);
    memory_region_init_io(&s->mmio[1], OBJECT(s), &rl78_sau_ops1, s,
                          "rl78-sau-mmio[1]", 0x04);
    memory_region_init_io(&s->mmio[2], OBJECT(s), &rl78_sau_ops2, s,
                          "rl78-sau-mmio[2]", 0x40);

    sysbus_init_mmio(d, &s->mmio[0]);
    sysbus_init_mmio(d, &s->mmio[1]);
    sysbus_init_mmio(d, &s->mmio[2]);

    s->inclk =
        qdev_init_clock_in(dev, "inclk", rl78_sau_update_inclk, s, ClockUpdate);

    qdev_init_gpio_out_named(dev, s->irqs, "irq", RL78_SAU_CHANNEL_NUM);
    qdev_init_gpio_out_named(dev, s->irq_errs, "irq-err", RL78_SAU_CHANNEL_NUM);

    for (int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        timer_init_ns(&s->channels[ch].interval_timer, QEMU_CLOCK_VIRTUAL,
                      tx_timer_up_callbacks[ch], s);
    }

    transmit_port_add(obj, "tx", s->tx_ports, RL78_SAU_CHANNEL_NUM);
    receive_port_add(obj, "rx", rl78_sau_rx_irq, RL78_SAU_CHANNEL_NUM);

    for(int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        s->channels[ch].rx_data_queue = g_queue_new();
    }
}

static void rl78_sau_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    RL78SAUClass *sc    = RL78_SAU_CLASS(klass);

    resettable_class_set_parent_phases(rc, NULL, rl78_sau_reset_hold, NULL,
                                       &sc->parent_phases);
}

static const TypeInfo rl78_sau_info = {
    .name          = TYPE_RL78_SAU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RL78SAUState),
    .instance_init = rl78_sau_init,
    .class_size    = sizeof(RL78SAUClass),
    .class_init    = rl78_sau_class_init,
};

static void rl78_sau_register_types(void)
{
    type_register_static(&rl78_sau_info);
}

type_init(rl78_sau_register_types)
