#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/irq.h"
#include "hw/core/registerfields.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/core/qdev-clock.h"
#include "migration/vmstate.h"
#include "exec/hwaddr.h"
#include "hw/rl78/clock.h"

REG8(CMC, 0xFFFA0)
    FIELD(CMC, AMPH, 0, 1)
    FIELD(CMC, AMPHS0, 1, 1)
    FIELD(CMC, AMPHS1, 2, 1)
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
REG8(OSMC, 0xF00F3)
    FIELD(OSMC, HIPREC, 0, 1)
    FIELD(OSMC, WUTMMCK, 4, 1)
    FIELD(OSMC, RTCLPC, 7, 1)
REG8(HOCODIV, 0xF00A8)
    FIELD(HOCODIV, HOCODIV, 0, 3)
REG8(MOCODIV, 0xF00F2)
    FIELD(MOCODIV, MOCODIV, 0, 2)
REG8(MOSCDIV, 0xF0214)
    FIELD(MOSCDIV, MOSCIDV, 0, 3)
REG8(HIOTRM, 0xF00A0)
    FIELD(HIOTRM, HIOTRM, 0, 6)
REG8(MIOTRM, 0xF0212)
REG8(LIOTRM, 0xF0213)
REG8(WKUPMD, 0xF0215)
    FIELD(WKUPMD, WKUPMD, 0, 1)

static const ClockPortInitArray rl78_clocks = {
    QDEV_CLOCK_OUT(RL78ClockGenerator, high_osc),
    QDEV_CLOCK_OUT(RL78ClockGenerator, middle_osc),
    QDEV_CLOCK_OUT(RL78ClockGenerator, low_osc),
    QDEV_CLOCK_END,
};

static void rl78_clock_write(void *opaque, hwaddr offset, uint64_t value, unsigned size) {

}

static uint64_t rl78_clock_read(void *opaque, hwaddr offset, unsigned size) {
    return 0;
}

static const MemoryRegionOps rl78_clock_ops = {
    .write = rl78_clock_write,
    .read = rl78_clock_read,
};

static void rl78_clock_class_init(ObjectClass *klass, const void *data)
{
    return;
}

static void rl78_clock_init(Object *obj) 
{

    qdev_init_clocks(DEVICE(obj), rl78_clocks);
}


