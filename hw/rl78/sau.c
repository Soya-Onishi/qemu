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

static uint32_t rl78_sau_send_bitlength(RL78SAUState *s, uint32_t channel)
{
    uint32_t bitlength;
    switch (FIELD_EX16(s->scr[channel], SCR, DLS)) {
    case 1:
        bitlength = 9;
        break;
    case 2:
        bitlength = 7;
        break;
    case 3:
        bitlength = 8;
        break;
    default:
        // TODO: assertion
        bitlength = 8;
        break;
    }

    // TODO: support SPI/I2C mode
    // Currently, only UART mode is expected.
    switch (FIELD_EX16(s->scr[channel], SCR, SLC)) {
    // Startbit + No Stopbit
    case 0:
        bitlength += 1;
        break;
    // Startbit + One Stopbit
    case 1:
        bitlength += 2;
        break;
    // Startbit + Two Stopbits
    case 2:
        bitlength += 3;
        break;
    default:
        // TODO: assertion
        bitlength += 2;
        break;
    }

    switch (FIELD_EX16(s->scr[channel], SCR, PTC)) {
    // No Parity
    case 0:
        break;
    // Use Parity(Zero/Odd/Even)
    case 1:
    case 2:
    case 3:
        bitlength += 1;
        break;
    default:
        // TODO: implementation bug assertion
        break;
    }

    return bitlength;
}

static void rl78_sau_send_byte(RL78SAUState *s, uint channel)
{
    const uint32_t clk_duration_ns =
        (1000 * 1000 * 1000) / s->fTCLK_hz[channel];
    const uint32_t bitlength        = rl78_sau_send_bitlength(s, channel);
    const uint32_t send_duration_ns = bitlength * clk_duration_ns;
    uint16_t txdata                 = s->sdr[channel] & 0x01FF;
    switch(FIELD_EX16(s->scr[channel], SCR, DLS)) {
        default:
        case 3:
            txdata = txdata & 0x0FF;
            break;
        case 2:
            txdata = txdata & 0x07F;
            break;
        case 1:
            txdata = txdata & 0x1FF;
            break;
    }

    const bool is_se  = !!(s->se & (1 << channel));
    const bool is_soe = !!(s->soe & (1 << channel));

    // TODO: inspect actual MCU movement when incorrect usage.
    // TODO: Is SOE needed for sending data without clock like UART?
    if (!is_se || !is_soe) {
        // TODO: assertion
        return;
    }

    s->ssr[channel] = FIELD_DP16(s->ssr[channel], SSR, BFF, 0);
    s->ssr[channel] = FIELD_DP16(s->ssr[channel], SSR, TSF, 1);

    const uint64_t expire_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + send_duration_ns;
    timer_mod(&s->tx_timer[channel], expire_time);

    WirePayload payload;
    payload.type = WIRE_PAYLOAD_TYPE_SERIAL;
    payload.serial.type = SERIAL_PACKET_TYPE_UART;
    payload.serial.uart.payload = txdata;
    payload.serial.uart.stopbits = FIELD_EX16(s->scr[channel], SCR, SLC);

    switch(FIELD_EX16(s->scr[channel], SCR, PTC)) {
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

    transmit_port_payload(&s->tx_ports[channel], &payload);

    // TODO: check SO bit for checking valid initial signal status

    if ((FIELD_EX16(s->smr[channel], SMR, MD) & 0x01) == 1) { 
        // SDR empty interrupt
        qemu_set_irq(s->irq[channel], 1);
    }
}

static void rl78_sau_update_fTCLK(RL78SAUState *s, int channel)
{
    const uint32_t clk = clock_get_hz(s->inclk);

    const uint8_t prs0 = FIELD_EX16(s->sps, SPS, PRS0);
    const uint8_t prs1 = FIELD_EX16(s->sps, SPS, PRS1);

    const uint32_t ck0   = clk / (1 << prs0);
    const uint32_t ck1   = clk / (1 << prs1);
    const uint32_t fMCK  = FIELD_EX16(s->smr[channel], SMR, CKS) ? ck1 : ck0;
    // TODO: support external clock source
    const uint32_t fTCLK = FIELD_EX16(s->smr[channel], SMR, CCS) ? 0 : fMCK;

    s->fTCLK_hz[channel] = fTCLK;
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
    // TODO: value assertion
    const uint32_t prs0 = FIELD_EX16(value, SPS, PRS0);
    const uint32_t prs1 = FIELD_EX16(value, SPS, PRS1);

    s->sps = FIELD_DP16(s->sps, SPS, PRS0, prs0);
    s->sps = FIELD_DP16(s->sps, SPS, PRS1, prs1);

    for (int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        rl78_sau_update_fTCLK(s, ch);
    }
}

static void rl78_sau_update_smr(RL78SAUState *s, uint16_t value, uint channel)
{
    // TODO: value assertion

    if (s->se & (1 << channel)) {
        // TODO: assertion (forbid to update when SE bit = 1)
    }

    const uint16_t mask = R_SMR_MD_MASK | R_SMR_SIS_MASK | R_SMR_STS_MASK |
                          R_SMR_CCS_MASK | R_SMR_CKS_MASK;
    const uint16_t written = (value & mask) | (s->smr[channel] & ~mask);

    s->smr[channel] = written;

    rl78_sau_update_fTCLK(s, channel);
}

static void rl78_sau_update_scr(RL78SAUState *s, uint16_t value, uint channel)
{
    // TODO: value assertion
    // TODO: support difference between MCU pin count

    if (s->se & (1 << channel)) {
        // TODO: assertion (forbid to update when SE bit = 1)
    }

    const uint16_t mask = R_SCR_DLS_MASK | R_SCR_SLC_MASK | R_SCR_DIR_MASK |
                          R_SCR_PTC_MASK | R_SCR_EOC_MASK | R_SCR_CKP_MASK |
                          R_SCR_DAP_MASK | R_SCR_RXE_MASK | R_SCR_TXE_MASK;
    const uint16_t written = (value & mask) | (s->scr[channel] & ~mask);

    s->scr[channel] = written;
}

static void rl78_sau_update_sdr(RL78SAUState *s, uint16_t value, uint channel)
{
    // TODO: value assertion for specific register
    // TODO: assert specific value assertion when UART mode and I2C mode

    const bool is_running = !!(s->se & (1 << channel));

    // not overwrite divisor setting when communication is running (SE bit = 1)
    if (is_running) {
        value &= 0x01FF;
        value |= s->sdr[channel] & 0xFE00;
    }

    const bool is_data_changed = (value & 0x01FF) != (s->sdr[channel] & 0x01FF);
    if (!is_running && is_data_changed) {
        // TODO: assertion (forbid to overwrite data when not running)
    }

    s->sdr[channel] = value;
    if (FIELD_EX16(s->ssr[channel], SSR, BFF)) {
        // TODO: assertion
        s->ssr[channel] = FIELD_DP16(s->ssr[channel], SSR, OVF, 1);
    }
    if (FIELD_EX16(s->scr[channel], SCR, TXE)) {
        s->ssr[channel] = FIELD_DP16(s->ssr[channel], SSR, BFF, 1);
    }

    const bool is_sending = !!FIELD_EX16(s->ssr[channel], SSR, TSF);
    if (FIELD_EX16(s->scr[channel], SCR, TXE) && !is_sending) {
        rl78_sau_send_byte(s, channel);
    }
}

static void rl78_sau_update_sir(RL78SAUState *s, uint16_t value, uint channel)
{
    // TODO: value assertion
    const uint16_t filter =
        value & (R_SIR_OVCT_MASK | R_SIR_PECT_MASK | R_SIR_FECT_MASK);
    s->ssr[channel] &= ~filter;
}

static void rl78_sau_update_ssr(RL78SAUState *s, uint16_t value, uint channel)
{
    // TODO: readonly register. invalid write access assertion.
}

static void rl78_sau_update_ss(RL78SAUState *s, uint16_t value)
{
    // RL78 SS register requires 4 clocks delay to set SS bit = 1
    // after setting SCR register RXE bit = 1
    // However, in QEMU, there is no such restrictions for simplicity.

    // TODO: value assertion

    const uint16_t mask  = R_SS_SS_MASK;
    s->se               |= value & mask;

    for (int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        if (value & (1 << ch)) {
            s->ssr[ch] = FIELD_DP16(s->ssr[ch], SSR, TSF, 0);
            s->ssr[ch] = FIELD_DP16(s->ssr[ch], SSR, BFF, 0);

            /**
             * TODO: inspect actual MCU movement
             *       if resetting SS bit = 1 when TX is running.
             */
            timer_del(&s->tx_timer[ch]);
        }
    }
}

static void rl78_sau_update_st(RL78SAUState *s, uint16_t value)
{
    // TODO: value assertion

    const uint16_t mask  = R_ST_ST_MASK;
    s->se               &= ~(value & mask);

    for (int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        if (value & (1 << ch)) {
            s->ssr[ch] = FIELD_DP16(s->ssr[ch], SSR, TSF, 0);
            s->ssr[ch] = FIELD_DP16(s->ssr[ch], SSR, BFF, 0);
            /**
             * TODO: inspect actual MCU movement
             *       if resetting ST bit = 1 when TX is running.
             */
            timer_del(&s->tx_timer[ch]);
        }
    }
}

static void rl78_sau_update_se(RL78SAUState *s, uint16_t value)
{
    // TODO: readonly register. invalid write access assertion.
}

static void rl78_sau_update_soe(RL78SAUState *s, uint16_t value)
{
    // TODO: value assertion

    const uint16_t mask = R_SOE_SOE_MASK;
    s->soe              = value & mask;
}

static void rl78_sau_update_so(RL78SAUState *s, uint16_t value)
{
    // TODO: value assertion
    const uint16_t so =
        (value & R_SO_SO_MASK & (~s->soe)) | (s->so & R_SO_SO_MASK & s->soe);
    const uint16_t cko = (value & R_SO_CKO_MASK);

    s->so = so | cko;
}

static void rl78_sau_update_sol(RL78SAUState *s, uint16_t value)
{
    // TODO: value assertion
    // TODO: assert when not zero value on non UART mode
    s->sol = value & 0x0005;
}

static void rl78_sau_update_ssc(RL78SAUState *s, uint16_t value)
{
    // TODO: support this register

    // Currently, SNOOZE mode serial data receive is not supported.
    // assign zero in force.
    s->ssc = 0;
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

static uint16_t rl78_sau_read_sps(RL78SAUState *s) { return s->sps & 0x00FF; }

static uint16_t rl78_sau_read_smr(RL78SAUState *s, uint channel)
{
    const uint16_t mask = R_SMR_MD_MASK | R_SMR_SIS_MASK | R_SMR_STS_MASK |
                          R_SMR_CCS_MASK | R_SMR_CKS_MASK;
    return (s->smr[channel] & mask) | 0x0020;
}

static uint16_t rl78_sau_read_scr(RL78SAUState *s, uint channel)
{
    const uint16_t mask = R_SCR_DLS_MASK | R_SCR_SLC_MASK | R_SCR_DIR_MASK |
                          R_SCR_PTC_MASK | R_SCR_EOC_MASK | R_SCR_CKP_MASK |
                          R_SCR_DAP_MASK | R_SCR_RXE_MASK | R_SCR_TXE_MASK;

    return (s->scr[channel] & mask) | 0x0004;
}

static uint16_t rl78_sau_read_sdr(RL78SAUState *s, uint channel)
{
    uint16_t sdr = s->sdr[channel];

    const bool is_rx = FIELD_EX16(s->scr[channel], SCR, RXE) == 1;

    if (s->se & (1 << channel) && is_rx) {
        sdr &= 0x01FF;
        s->ssr[channel] = FIELD_DP16(s->ssr[channel], SSR, BFF, 0);

        if(!g_queue_is_empty(s->rx_data_queue[channel])) {
            uint16_t* data = g_queue_pop_head(s->rx_data_queue[channel]);

            s->ssr[channel] = FIELD_DP16(s->ssr[channel], SSR, BFF, 1);
            s->sdr[channel] = deposit32(s->sdr[channel], 0, 9, *data);
            qemu_set_irq(s->irq[channel], 1);

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
    return s->ssr[channel];
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

static uint16_t rl78_sau_read_se(RL78SAUState *s) { return s->se & 0x000F; }

static uint16_t rl78_sau_read_soe(RL78SAUState *s) { return s->soe & 0x000F; }

static uint16_t rl78_sau_read_so(RL78SAUState *s)
{
    // TODO: support for reflecting output signal bit
    // Currently, SO register just returns the value set by guest code.
    return s->so & 0x0F0F;
}

static uint16_t rl78_sau_read_sol(RL78SAUState *s) { return s->sol & 0x0005; }

static uint16_t rl78_sau_read_ssc(RL78SAUState *s) { return s->ssc; }

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

    s->sps = 0x0000;
    s->se  = 0x0000;
    s->soe = 0x0000;
    s->so  = 0x0F0F;
    s->sol = 0x0000;
    s->ssc = 0x0000;
    for (int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        s->smr[ch] = 0x0020;
        s->scr[ch] = 0x0087;
        s->sdr[ch] = 0x0000;
        s->ssr[ch] = 0x0000;
    }
}

static void rl78_sau_tx_timer_up(RL78SAUState *s, int channel)
{
    const bool is_txen  = !!FIELD_EX16(s->scr[channel], SCR, TXE);
    const bool has_data = !!FIELD_EX16(s->ssr[channel], SSR, BFF);

    s->ssr[channel] = FIELD_DP16(s->ssr[channel], SSR, TSF, 0);
    if (is_txen && has_data) {
        rl78_sau_send_byte(s, channel);
    }

    if ((FIELD_EX16(s->smr[channel], SMR, MD) & 0x01) == 0) {
        // TX done interrupt
        qemu_irq_pulse(s->irq[channel]);
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

    const WirePayload *p = (const WirePayload *)payload;
    // TODO: raise SRE interrupt if serial signal format is unmatched
 
    const bool is_uart = extract16(s->smr[index], 1, 2) == 1;
    if((index & 0x01) && is_uart) {
        qemu_log_mask(LOG_GUEST_ERROR, "UART signal must be received on even channel.\n");
    }

    if(is_uart) {
        index |= 0x01;
    }

    if(!(s->se & (1 << index))) { 
        // If not enabled, ignore the received data
        return;
    }

    if(is_uart && extract16(s->scr[index], 1, 2) == 1) {
        qemu_log_mask(LOG_GUEST_ERROR, "When using UART mode, SMRm%lu and SMRm%lu must be UART", index - 1, index);
        // only report error, and continue processing
    }
    
    if(FIELD_EX16(s->scr[index], SCR, RXE) == 0) {
        // If not RX enabled, ignore the received data
        qemu_log_mask(LOG_GUEST_ERROR, "SCRm%lu is not RX enabled.\n", index);
        return;
    } 

    // Actual MCU, TSF bit is asserted when receiving data, 
    // but QEMU receives byte data at once, so TSF bit is not asserted.
    
    const uint16_t rxdata = p->serial.uart.payload;

    if(FIELD_EX16(s->ssr[index], SSR, BFF)) {
        uint16_t* data = g_new(uint16_t, 1);
        *data = rxdata;
        g_queue_push_tail(s->rx_data_queue[index], data);
    } else { 
        s->ssr[index] = FIELD_DP16(s->ssr[index], SSR, BFF, 1);
        s->sdr[index] = deposit32(s->sdr[index], 0, 9, rxdata);
        qemu_set_irq(s->irq[index], 1);
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

    qdev_init_gpio_out_named(dev, s->irq, "irq", RL78_SAU_CHANNEL_NUM);
    qdev_init_gpio_out_named(dev, s->irq_err, "irq-err", RL78_SAU_CHANNEL_NUM);

    for (int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        timer_init_ns(&s->tx_timer[ch], QEMU_CLOCK_VIRTUAL,
                      tx_timer_up_callbacks[ch], s);
    }

    transmit_port_add(obj, "tx", s->tx_ports, RL78_SAU_CHANNEL_NUM);
    receive_port_add(obj, "rx", rl78_sau_rx_irq, RL78_SAU_CHANNEL_NUM);

    for(int ch = 0; ch < RL78_SAU_CHANNEL_NUM; ch++) {
        s->rx_data_queue[ch] = g_queue_new();
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
