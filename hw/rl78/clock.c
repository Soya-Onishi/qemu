#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/loader.h"
#include "hw/core/registerfields.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/resettable.h"
#include "exec/hwaddr.h"
#include "qom/object.h"
#include "hw/rl78/clock.h"

struct RL78ClockClass {
    /* private */
    SysBusDeviceClass parent_class;

    /* public */
    ResettablePhases parent_phases;
};
typedef struct RL78ClockClass RL78ClockClass;

DECLARE_CLASS_CHECKERS(RL78ClockClass, RL78_CLOCK, TYPE_RL78_CLOCK)

REG8(CMC, 0xFFFA0)
FIELD(CMC, AMPH, 0, 1)
FIELD(CMC, AMPHS, 2, 1)
FIELD(CMC, XTSEL, 3, 1)
FIELD(CMC, OSCSELS, 4, 1)
FIELD(CMC, EXCLKS, 5, 1)
FIELD(CMC, OSCSEL, 6, 1)
FIELD(CMC, EXCLK, 7, 1)
REG8(CSC, 0xFFFA1)
FIELD(CSC, HIOSTOP, 0, 1)
FIELD(CSC, MIOEN, 1, 1)
FIELD(CSC, XTSTOP, 6, 1)
FIELD(CSC, MSTOP, 7, 1)
REG8(OSTC, 0xFFFA2)
REG8(OSTS, 0xFFFA3)
FIELD(OSTS, OSTS, 0, 3)
REG8(CKC, 0xFFFA4)
FIELD(CKC, MCM1, 0, 1)
FIELD(CKC, MCS1, 1, 1)
FIELD(CKC, MCM0, 4, 1)
FIELD(CKC, MCS, 5, 1)
FIELD(CKC, CSS, 6, 1)
FIELD(CKC, CLS, 7, 1)
REG8(CKS0, 0xFFFA5)
FIELD(CKS0, CCS, 0, 3)
FIELD(CKS0, CSEL, 3, 1)
FIELD(CKS0, PCLOE, 7, 1)
REG8(CKS1, 0xFFFA6)
FIELD(CKS1, CCS, 0, 3)
FIELD(CKS1, CSEL, 3, 1)
FIELD(CKS1, PCLOE, 7, 1)
REG8(CKSEL, 0xFFFA7)
FIELD(CKSEL, SELLOSC, 0, 1)
REG8(MOCODIV, 0xF00F2)
FIELD(MOCODIV, MOCODIV, 0, 2)
REG8(OSMC, 0xF00F3)
FIELD(OSMC, HIPREC, 0, 1)
FIELD(OSMC, WUTMMCK, 4, 1)
FIELD(OSMC, RTCLPC, 7, 1)
REG8(HIOTRM, 0xF00A0)
FIELD(HIOTRM, HIOTRM, 0, 6)
REG8(HOCODIV, 0xF00A8)
FIELD(HOCODIV, HOCODIV, 0, 3)
REG8(MIOTRM, 0xF0212)
REG8(LIOTRM, 0xF0213)
REG8(MOSCDIV, 0xF0214)
FIELD(MOSCDIV, MOSCIDV, 0, 3)
REG8(WKUPMD, 0xF0215)
FIELD(WKUPMD, WKUPMD, 0, 1)

static const ClockPortInitArray rl78_clocks = {
    QDEV_CLOCK_OUT(RL78ClockState, fIHP),
    QDEV_CLOCK_OUT(RL78ClockState, fIMP),
    QDEV_CLOCK_OUT(RL78ClockState, fMXP),
    QDEV_CLOCK_OUT(RL78ClockState, fCLK),
    QDEV_CLOCK_OUT(RL78ClockState, fMAIN),
    QDEV_CLOCK_OUT(RL78ClockState, fIL),
    QDEV_CLOCK_OUT(RL78ClockState, fSXP),
    QDEV_CLOCK_OUT(RL78ClockState, fRTCCK),
    QDEV_CLOCK_END,
};

static const uint32_t rl78_clock_high_osc_freq_table_mhz[2][8] = {
    {
        [0] = 24,
        [1] = 12,
        [2] = 6,
        [3] = 3,
        [4] = 24, // invalid setting
        [5] = 24, // invalid setting
        [6] = 24, // invalid setting
        [7] = 24, // invalid setting
    },
    {
        [0] = 32,
        [1] = 16,
        [2] = 8,
        [3] = 4,
        [4] = 2,
        [5] = 1,
        [6] = 32, // invalid setting
        [7] = 32, // invalid setting
    },
};

static const uint32_t rl78_clock_mid_osc_freq_table_mhz[4] = {
    [0] = 4,
    [1] = 2,
    [2] = 1,
    [3] = 4, // invalid setting
};

static void rl78_clock_update_commit(RL78ClockState *s)
{
    const uint32_t MHZ = 1000 * 1000;
    const uint32_t high_osc_mhz =
        rl78_clock_high_osc_freq_table_mhz[s->frqsel3][s->hocodiv];
    const uint32_t mid_osc_mhz = rl78_clock_mid_osc_freq_table_mhz[s->mocodiv];

    const uint32_t low_osc = 32768;
    const uint32_t high_osc =
        FIELD_EX8(s->csc, CSC, HIOSTOP) ? 0 : high_osc_mhz * MHZ;
    const uint32_t mid_osc =
        FIELD_EX8(s->csc, CSC, MIOEN) ? mid_osc_mhz * MHZ : 0;

    // TODO: support XT1 clock
    const uint32_t fSUB = FIELD_EX8(s->cksel, CKSEL, SELLOSC)
                              ? 0
                              : low_osc; 
    const uint32_t fOCO = FIELD_EX8(s->ckc, CKC, MCM1) ? mid_osc : high_osc;

    // TODO: support X1 clock
    const uint32_t fMAIN =
        FIELD_EX8(s->ckc, CKC, MCM0) ? 0 : fOCO; 
    const uint32_t fCLK = FIELD_EX8(s->ckc, CKC, MCS) ? fSUB : fMAIN;
    const uint32_t fIL  = low_osc;

    // TODO support XT1 clock
    const uint32_t fSXP   = FIELD_EX8(s->osmc, OSMC, WUTMMCK)
                                ? low_osc
                                : 0; 
    // TODO support XT1 clock
    const uint32_t fRTCCK = FIELD_EX8(s->osmc, OSMC, WUTMMCK)
                                ? low_osc
                                : 0; 

    // TODO: support X1 clock
    const uint32_t fIHP = high_osc;
    const uint32_t fIMP = mid_osc;
    const uint32_t fMXP = 0; 

    clock_update_hz(s->fIHP, fIHP);
    clock_update_hz(s->fIMP, fIMP);
    clock_update_hz(s->fMXP, fMXP);
    clock_update_hz(s->fCLK, fCLK);
    clock_update_hz(s->fMAIN, fMAIN);
    clock_update_hz(s->fIL, fIL);
    clock_update_hz(s->fSXP, fSXP);
    clock_update_hz(s->fRTCCK, fRTCCK);
}

static void rl78_clock_update_cmc(RL78ClockState *s, uint8_t value)
{
    if (s->cmc_dirty || s->cmc == value) {
        return;
    }

    s->cmc       = value;
    s->cmc_dirty = true;
}

static void rl78_clock_update_ckc(RL78ClockState *s, uint8_t value)
{
    // TODO: implement value assertion
    // TODO: implement ADC and Serial peripheral feature warning assertion

    const uint8_t readonly_mask =
        (R_CKC_CLS_MASK | R_CKC_MCS_MASK | R_CKC_MCS1_MASK | 0xC0);
    const uint8_t written = (value & readonly_mask) | s->ckc;

    // readonly status register bits make no delays to reflect the changes
    s->ckc = written;
    FIELD_DP8(s->ckc, CKC, CLS, FIELD_EX8(written, CKC, CSS));
    FIELD_DP8(s->ckc, CKC, MCS, FIELD_EX8(written, CKC, MCM0));
    FIELD_DP8(s->ckc, CKC, MCS1, FIELD_EX8(written, CKC, MCM1));
}

static void rl78_clock_update_csc(RL78ClockState *s, uint8_t value)
{
    // TODO: implement value assertion
    const uint8_t mask = R_CSC_MSTOP_MASK | R_CSC_XTSTOP_MASK |
                         R_CSC_HIOSTOP_MASK | R_CSC_MIOEN_MASK;
    const uint8_t written = value & mask;

    s->csc = written;
}

static void rl78_clock_update_ostc(RL78ClockState *s, uint8_t value)
{
    // This is readonly register
    // TODO: implement write assertion

    return;
}

static void rl78_clock_update_osts(RL78ClockState *s, uint8_t value)
{
    // TODO: assert when MSTOP bit == 0
    // TODO: value assertion

    // For ease of implementation, there is no wait when setting MSTOP into 0,
    // so this register setting should not have any effects.

    s->osts = value & R_OSTS_OSTS_MASK;
}

static void rl78_clock_update_cks0(RL78ClockState *s, uint8_t value)
{
    // TODO: implement
    s->cks0 = value;
}

static void rl78_clock_update_cks1(RL78ClockState *s, uint8_t value)
{
    // TODO: implement
    s->cks1 = value;
}

static void rl78_clock_update_osmc(RL78ClockState *s, uint8_t value)
{
    // TODO: value assertion
    // TODO: setting WUTMMCK bit when subsystem clock is running is forbidden
    const uint8_t mask    = R_OSMC_RTCLPC_MASK | R_OSMC_WUTMMCK_MASK;
    const uint8_t written = (value & mask) | (s->osmc & ~mask);

    s->osmc = written;
}

static void rl78_clock_update_cksel(RL78ClockState *s, uint8_t value)
{
    // TODO: value assertion
    const uint8_t mask = R_CKSEL_SELLOSC_MASK;

    s->cksel = (value & mask) | (s->cksel & ~mask);
}

static void rl78_clock_update_hocodiv(RL78ClockState *s, uint8_t value)
{
    // TODO: value assertion
    s->hocodiv = FIELD_EX8(value, HOCODIV, HOCODIV);
}

static void rl78_clock_update_mocodiv(RL78ClockState *s, uint8_t value)
{
    // TODO: value assertion
    s->mocodiv = FIELD_EX8(value, MOCODIV, MOCODIV);
}

static void rl78_clock_update_moscdiv(RL78ClockState *s, uint8_t value)
{
    // TODO: value assertion
    s->moscdiv = FIELD_EX8(value, MOSCDIV, MOSCIDV);
}

static void rl78_clock_update_hiotrm(RL78ClockState *s, uint8_t value)
{
    // TODO: value assertion
    s->hiotrm = FIELD_EX8(value, HIOTRM, HIOTRM);
}

static void rl78_clock_update_miotrm(RL78ClockState *s, uint8_t value)
{
    s->miotrm = value;
}

static void rl78_clock_update_liotrm(RL78ClockState *s, uint8_t value)
{
    s->liotrm = value;
}

static void rl78_clock_update_wkupmd(RL78ClockState *s, uint8_t value)
{
    // TODO: value assertion
    // TODO: reset when exiting STOP mode
    const uint8_t mask = R_WKUPMD_WKUPMD_MASK;
    s->wkupmd          = (value & mask) | (s->wkupmd & ~mask);
}

static void rl78_clock_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned size)
{
    RL78ClockState *s = RL78_CLOCK(opaque);
    switch (addr) {
    case 0xFFFA0:
        rl78_clock_update_cmc(s, value);
        break;
    case 0xFFFA1:
        rl78_clock_update_csc(s, value);
        break;
    case 0xFFFA2:
        rl78_clock_update_ostc(s, value);
        break;
    case 0xFFFA3:
        rl78_clock_update_osts(s, value);
        break;
    case 0xFFFA4:
        rl78_clock_update_ckc(s, value);
        break;
    case 0xFFFA5:
        rl78_clock_update_cks0(s, value);
        break;
    case 0xFFFA6:
        rl78_clock_update_cks1(s, value);
        break;
    case 0xFFFA7:
        rl78_clock_update_cksel(s, value);
        break;
    case 0xF00F2:
        rl78_clock_update_mocodiv(s, value);
        break;
    case 0xF00F3:
        rl78_clock_update_osmc(s, value);
        break;
    case 0xF00A0:
        rl78_clock_update_hiotrm(s, value);
        break;
    case 0xF00A8:
        rl78_clock_update_hocodiv(s, value);
        break;
    case 0xF0212:
        rl78_clock_update_miotrm(s, value);
        break;
    case 0xF0213:
        rl78_clock_update_liotrm(s, value);
        break;
    case 0xF0214:
        rl78_clock_update_moscdiv(s, value);
        break;
    case 0xF0215:
        rl78_clock_update_wkupmd(s, value);
        break;
    default:
        // TODO: invalid access assertion
        break;
    }

    rl78_clock_update_commit(s);
}

static uint64_t rl78_clock_read_ckc(RL78ClockState *s)
{
    uint8_t ckc = s->ckc;

    // In QEMU, there is no delay to switch clock multiplexer.
    // status bits are always same as corresponding settings.
    FIELD_DP8(ckc, CKC, CLS, FIELD_EX8(ckc, CKC, CSS));
    FIELD_DP8(ckc, CKC, MCS, FIELD_EX8(ckc, CKC, MCM0));
    FIELD_DP8(ckc, CKC, MCS1, FIELD_EX8(ckc, CKC, MCM1));

    return ckc;
}

static uint64_t rl78_clock_read_ostc(RL78ClockState *s) { return 0; }

static uint64_t rl78_clock_read_osmc(RL78ClockState *s)
{
    uint8_t osmc = s->osmc;

    // make HIPREC bit always 1
    // In QEMU, high speed oscillator always finish warming up.
    FIELD_DP8(osmc, OSMC, HIPREC, 1);

    return osmc;
}

static uint64_t rl78_clock_read(void *opaque, hwaddr addr, unsigned size)
{
    RL78ClockState *s = RL78_CLOCK(opaque);
    switch (addr) {
    case 0xFFFA0:
        return s->cmc;
    case 0xFFFA1:
        return s->csc;
    case 0xFFFA2:
        return rl78_clock_read_ostc(s);
    case 0xFFFA3:
        return s->osts;
    case 0xFFFA4:
        return rl78_clock_read_ckc(s);
    case 0xFFFA5:
        return s->cks0;
    case 0xFFFA6:
        return s->cks1;
    case 0xFFFA7:
        return s->cksel;
    case 0xF00F2:
        return s->mocodiv;
    case 0xF00F3:
        return rl78_clock_read_osmc(s);
    case 0xF00A0:
        return s->hiotrm;
    case 0xF00A8:
        return s->hocodiv;
    case 0xF0212:
        return s->miotrm;
    case 0xF0213:
        return s->liotrm;
    case 0xF0214:
        return s->moscdiv;
    case 0xF0215:
        return s->wkupmd;
    default:
        // TODO: invalid access assertion
        return 0;
    }
}

#define RL78_CLOCK_OPS(idx, base_addr, mmio_size)                              \
    static void rl78_clock_write_##idx(void *opaque, hwaddr offset,            \
                                       uint64_t value, unsigned size)          \
    {                                                                          \
        hwaddr addr = base_addr + offset;                                      \
        rl78_clock_write(opaque, addr, value, size);                           \
    }                                                                          \
    static uint64_t rl78_clock_read_##idx(void *opaque, hwaddr offset,         \
                                          unsigned size)                       \
    {                                                                          \
        hwaddr addr = base_addr + offset;                                      \
        return rl78_clock_read(opaque, addr, size);                            \
    }                                                                          \
    static const MemoryRegionOps rl78_clock_ops_##idx = {                      \
        .write                 = rl78_clock_write_##idx,                       \
        .read                  = rl78_clock_read_##idx,                        \
        .valid.max_access_size = 1,                                            \
        .valid.min_access_size = 1,                                            \
        .impl.min_access_size  = 1,                                            \
        .impl.max_access_size  = 1,                                            \
    };                                                                         \
    static void rl78_clock_init_mmio_##idx(RL78ClockState *s)                  \
    {                                                                          \
        SysBusDevice *d = SYS_BUS_DEVICE(s);                                   \
        memory_region_init_io(&s->mmio##idx, OBJECT(s), &rl78_clock_ops_##idx, \
                              s, "rl78-clock-mmio-" #idx, mmio_size);          \
        sysbus_init_mmio(d, &s->mmio##idx);                                    \
    }

RL78_CLOCK_OPS(0, 0xFFFA0, 8);
RL78_CLOCK_OPS(1, 0xF00F2, 2);
RL78_CLOCK_OPS(2, 0xF00A0, 9);
RL78_CLOCK_OPS(3, 0xF0212, 4);

static void rl78_clock_reset_hold(Object *obj, ResetType type)
{
    // TODO: support boot swap
    RL78ClockState *s = RL78_CLOCK(obj);

    uint8_t *option_byte_ptr = rom_ptr(0x000C2, 1);
    uint8_t frqsel0_2        = 0;
    uint8_t frqsel3          = 1;
    // TODO: assert invalid frequency settings for specific CMODE
    if (option_byte_ptr) {
        frqsel0_2 = *option_byte_ptr & 0x07;
        frqsel3   = *option_byte_ptr & 0x08 ? 1 : 0;
    }

    s->cmc     = 0x00;
    s->ckc     = 0x00;
    s->csc     = 0xC0;
    s->osts    = 0x07;
    s->cks0    = 0x00;
    s->cks1    = 0x00;
    s->osmc    = 0x01;
    s->cksel   = 0x00;
    s->hocodiv = frqsel0_2;
    s->mocodiv = 0x00;
    s->moscdiv = 0x00;
    s->hiotrm  = 0x20; // In QEMU, HIOTRM is 0x20 as factory default setting
    s->miotrm  = 0x90;
    s->liotrm  = 0x80;
    s->wkupmd  = 0x00;

    s->frqsel3   = frqsel3;
    s->cmc_dirty = false;

    rl78_clock_update_commit(s);
}

static void rl78_clock_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    RL78ClockState *s = RL78_CLOCK(obj);

    qdev_init_clocks(dev, rl78_clocks);

    rl78_clock_init_mmio_0(s);
    rl78_clock_init_mmio_1(s);
    rl78_clock_init_mmio_2(s);
    rl78_clock_init_mmio_3(s);
}

static void rl78_clock_class_init(ObjectClass *klass, const void *data)
{
    RL78ClockClass *cc  = RL78_CLOCK_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    resettable_class_set_parent_phases(rc, NULL, rl78_clock_reset_hold, NULL,
                                       &cc->parent_phases);
}

static const TypeInfo rl78_clock_info = {
    .name          = TYPE_RL78_CLOCK,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RL78ClockState),
    .instance_init = rl78_clock_init,
    .class_init    = rl78_clock_class_init,
    .class_size    = sizeof(RL78ClockClass),
};

static void rl78_clock_register_types(void)
{
    type_register_static(&rl78_clock_info);
}

type_init(rl78_clock_register_types)
