#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/char/rl78_sau.h"
#include "migration/vmstate.h"

#define SAU_REG(name, offset)   \
    REG16(name##0, offset+0)    \
    REG16(name##1, offset+2)    \
    REG16(name##2, offset+4)    \
    REG16(name##3, offset+6)    

#define SAU_FIELD(reg, field, offset, length) \
    FIELD(reg##0, field, offset, length) \
    FIELD(reg##1, field, offset, length) \
    FIELD(reg##2, field, offset, length) \
    FIELD(reg##3, field, offset, length) 

/* SFR Region Registers */
REG16(SDR0, 0)
    FIELD(SDR0, DATA, 0, 9)
    FIELD(SDR0, FREQ, 9, 7)
REG16(SDR1, 8)
    FIELD(SDR1, DATA, 0, 9)
    FIELD(SDR1, FREQ, 9, 7)

/* 2nd SFR Region Registers */
REG16(SSR, 0x00)
    FIELD(SSR, OVF, 0, 1)
    FIELD(SSR, PEF, 1, 1)
    FIELD(SSR, FEF, 2, 1)
    FIELD(SSR, BFF, 5, 1)
    FIELD(SSR, TSF, 6, 1)
REG16(SIR, 0x02)
    FIELD(SIR, OVCT, 0, 1)
    FIELD(SIR, PECT, 1, 1)
    FIELD(SIR, FECT, 2, 1)
REG16(SMR, 0x04)
    FIELD(SMR, MD_INT, 0, 1)
    FIELD(SMR, MD_TYPE, 1, 2)
    FIELD(SMR, SIS, 6, 1)
    FIELD(SMR, STS, 8, 1)
    FIELD(SMR, CCS, 14, 1)
    FIELD(SMR, CKS, 15, 1)
REG16(SCR, 0x06)
    FIELD(SCR, DLS, 0, 2)
    FIELD(SCR, SLC, 4, 2)
    FIELD(SCR, DIR, 7, 1)
    FIELD(SCR, PTC, 8, 2)
    FIELD(SCR, EOC, 10, 1)
    FIELD(SCR, CKP, 12, 1)
    FIELD(SCR, DAP, 13, 1)
    FIELD(SCR, RXE, 14, 1)
    FIELD(SCR, TXE, 15, 1)
REG16(SE, 0x08)
    FIELD(SE, SE0, 0, 1)    
    FIELD(SE, SE1, 1, 1)    
    FIELD(SE, SE2, 2, 1)    
    FIELD(SE, SE3, 3, 1)    
REG16(SS, 0x22)
    FIELD(SS, SS0, 0, 1)    
    FIELD(SS, SS1, 1, 1)    
    FIELD(SS, SS2, 2, 1)    
    FIELD(SS, SS3, 3, 1)    
REG16(ST, 0x24)
    FIELD(ST, ST0, 0, 1)    
    FIELD(ST, ST1, 1, 1)    
    FIELD(ST, ST2, 2, 1)    
    FIELD(ST, ST3, 3, 1)    
REG16(SPS, 0x26)
    FIELD(SPS, PRS0, 0, 4)    
    FIELD(SPS, PRS1, 4, 4)    
REG16(SO, 0x28)
    FIELD(SO, SO0, 0, 1)
    FIELD(SO, SO1, 1, 1)
    FIELD(SO, SO2, 2, 1)
    FIELD(SO, SO3, 3, 1)
    FIELD(SO, CKO0, 8, 1)
    FIELD(SO, CKO1, 9, 1)
    FIELD(SO, CKO2, 10, 1)
    FIELD(SO, CKO3, 11, 1)
REG16(SOE, 0x2A)
    FIELD(SOE, SOE0, 0, 1)
    FIELD(SOE, SOE1, 1, 1)
    FIELD(SOE, SOE2, 2, 1)
    FIELD(SOE, SOE3, 3, 1)
REG16(SOL, 0x2C)
    FIELD(SOL, SOL0, 0, 1)
    FIELD(SOL, SOL2, 2, 1)
REG16(SSC, 0x2E)
    FIELD(SSC, SWC, 0, 1)
    FIELD(SSC, SSEC, 1, 1)

static int can_receive(void *opaque)
{
    return 0;
}

static void send_byte(SAUState *sau, const unsigned ch)
{
    const unsigned baudrate_time = 521; // 19200bps
    if(qemu_chr_fe_backend_connected(&sau->chr[ch])) {
        const uint8_t data = sau->trx_data[ch] & 0x01FF;
        qemu_chr_fe_write_all(&sau->chr[ch], &data, 1);
    }

    sau->ssr[ch] |= R_SSR_TSF_MASK;
    timer_mod(&sau->timer[ch], qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + baudrate_time);
}

static void receive(void *opaque, const uint8_t *buf, int size)
{

}

static void sau_write_data0(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    SAUState *sau =  SAU(opaque);
    const unsigned data = val & 0x1FF;

    switch(offset) {
    case A_SDR0: 
        sau->trx_data[0] = data;
        if(size != 1) {
            // TODO: update clock division 
        }
        if(!(sau->ssr[0] & R_SSR_TSF_MASK)) {
            send_byte(sau, 0);
        }
        break;
    case A_SDR1: 
        sau->trx_data[1] = data;
        if(size != 1) {
            // TODO: update clock division 
        }
        if(!(sau->ssr[1] & R_SSR_TSF_MASK)) {
            send_byte(sau, 1);
        }
        break;
    default: 
        // TODO: error logging 
        break;
    }
}

static void sau_write_data1(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    SAUState *sau = SAU(opaque);
    const unsigned data = val & 0x1FF;

    switch(offset) {
    case A_SDR0:
        sau->trx_data[2] = data;
        if(size != 1) {
            // TODO: update clock division
        }
        if(!(sau->ssr[2] & R_SSR_TSF_MASK)) {
            send_byte(sau, 2);
        }
        break;
    case A_SDR1:
        sau->trx_data[3] = data;
        if(size != 1) {
            // TODO: update clock division
        }
        if(!(sau->ssr[3] & R_SSR_TSF_MASK)) {
            send_byte(sau, 3);
        }
        break;
    default:
        // TODO: error logging 
        break;
    }
}

static void update_ssr(uint16_t* ssr, const uint16_t val)
{
    const unsigned mask = val & 0x07;
    const unsigned filter = ~mask;

    *ssr &= filter;
}

static void set_se(uint16_t* se, const uint16_t val)
{
    /**
     * TODO: warning when bits are set in range of 
     *   - SS0 register bit15-4
     *   - SS1 register bit15-2(30-64pin)
     *   - SS1 register bit15-4(80-128pin)
     */ 
    /**
     * TODO: warning when RXEbit in SCR is not set
     */
    const unsigned mask = val & 0x0F;
    *se |= mask;
}

static void clr_se(uint16_t* se, const uint16_t val)
{
    /**
     * TODO: warning when bits are set in range of 
     *   - ST0 register bit15-4
     *   - ST1 register bit15-2(30-64pin)
     *   - ST1 register bit15-4(80-128pin)
     */ 
    const unsigned mask = ~(val & 0x0F);
    *se &= mask;
}

static void write_so(uint16_t* so, const uint16_t se, const uint16_t soe, const uint16_t val)
{
    const uint16_t so_mask = 0x0F & ~soe;
    const uint16_t cko_mask = (0x0F & ~se) << 8;
    const uint16_t mask = so_mask | cko_mask;
    const uint16_t data = val & mask;

    /**
     * TODO: SOEレジスタが0でないビットに対応する
     *       ビットの上書きを行っている場合、警告
     * TODO: SEレジスタの0でないビットに対応する
     *       ビットの上書きを行っている場合、警告
     */
    
     *so = data;
}

static void write_soe(uint16_t* soe, const uint16_t val)
{
    *soe = val & 0x000F;
}

static void sau_write_control(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    SAUState *sau =  SAU(opaque);
    const unsigned loc = (offset >> 2) & 0xFF;
    const unsigned channel = offset & 0x03;
    const uint16_t data = val & 0xFFFF;

    if(offset < 0x20) {
        switch(loc) {
        case A_SIR:  
            update_ssr(&sau->ssr[channel], data);
            break; 
        case A_SMR:
            // TODO
            break;
        case A_SCR:
            // TODO:
            break;
        case A_SSR: 
            /** TODO: warning for writing on readonly register */
        default:
            /** TODO: warning for unknown register */
            break;
        }
    } else {
        switch(offset) {
        case A_SS:
            set_se(&sau->se, data);
            break;
        case A_ST:
            clr_se(&sau->se, data);
            break;
        case A_SPS:
            /** TODO: implement */
            break;
        case A_SO:
            write_so(&sau->so, sau->se, sau->soe, data);
            break;
        case A_SOE:
            write_soe(&sau->soe, data);
            break;
        case A_SOL:
            /** TODO: implement */
            break;
        case A_SSC:
            /** TODO: implement */
            break;
        case A_SE:
        default:
            /** TODO: warning */
            break;
        }
    } 
}

static uint64_t sau_read_data0(void *opaque, hwaddr addr, unsigned size)
{
    // TODO: implement
    return 0;
}

static uint64_t sau_read_data1(void *opaque, hwaddr addr, unsigned size)
{
    // TODO: implement
    return 0;
}

static uint64_t sau_read_control(void *opaque, hwaddr offset, unsigned size)
{
    SAUState *sau =  SAU(opaque);
    const unsigned loc = (offset >> 2) & 0xFF;
    const unsigned channel = offset & 0x03;

    if(offset < 0x20) {
        switch(loc) {
        case A_SIR:  
            return 0;
        case A_SMR:
            return 0;
        case A_SCR:
            return 0;
        case A_SSR: 
            return sau->ssr[channel];
        default:
            /** TODO: warning for unknown register */
            break;
        }
    }
     
    return 0;
}

#define send_end_ch(ch) \
    static void send_end##ch(void* opaque)  \
    {                                       \
        SAUState* sau = SAU(opaque);        \
        send_end(sau, ch);                  \
    }

static void send_end(SAUState *sau, const unsigned ch)
{
    sau->ssr[ch] &= ~R_SSR_TSF_MASK;
    /** TODO: raise interrupt */
}

send_end_ch(0)
send_end_ch(1)
send_end_ch(2)
send_end_ch(3)

static void (*send_end_ch_fns[SAU_CHANNEL_NUM])(void*) = {
    send_end0,
    send_end1,
    send_end2,
    send_end3,
};

static const MemoryRegionOps sau_data0 = {
    .write = sau_write_data0,
    .read  = sau_read_data0,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.max_access_size = 2,
    .valid.max_access_size = 2,
};

static const MemoryRegionOps sau_data1 = {
    .write = sau_write_data1,
    .read = sau_read_data1,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.max_access_size = 2,
    .valid.max_access_size = 2,
};

static const MemoryRegionOps sau_control = {
    .write = sau_write_control,
    .read = sau_read_control,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.max_access_size = 2,
    .valid.max_access_size = 2,
};

static void sau_reset(DeviceState* dev) 
{
    // SAUState *sau = SAU()
}

static void sau_event(void *opaque, QEMUChrEvent event)
{
    // nothing to do
}

static void sau_realize(DeviceState *dev, Error **errp)
{
    SAUState *sau = SAU(dev);

    for(int i = 0; i < SAU_CHANNEL_NUM; i++) {
        qemu_chr_fe_set_handlers(&sau->chr[i], can_receive, receive, sau_event, NULL, sau, NULL, true);
    }
}

static void sau_init(Object *obj) 
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    SAUState *sau = SAU(obj);

    memory_region_init_io(&sau->data0, OBJECT(sau), &sau_data0, sau, "rl78-sau*-data0", 0x4);
    memory_region_init_io(&sau->data1, OBJECT(sau), &sau_data1, sau, "rl78-sau*-data1", 0x4);
    memory_region_init_io(&sau->control, OBJECT(sau), &sau_control, sau, "rl78-sau*-control", 0x40);

    sysbus_init_mmio(d, &sau->data0);
    sysbus_init_mmio(d, &sau->data1);
    sysbus_init_mmio(d, &sau->control);

    // TODO: Add irq

    for(int i = 0; i < SAU_CHANNEL_NUM; i++) {
        timer_init_ns(&sau->timer[i], QEMU_CLOCK_VIRTUAL, send_end_ch_fns[i], sau);
    }
}

static const Property sau_properties[] = {
    DEFINE_PROP_CHR("chardev[0]", SAUState, chr[0]),
    DEFINE_PROP_CHR("chardev[1]", SAUState, chr[1]),
    DEFINE_PROP_CHR("chardev[2]", SAUState, chr[2]),
    DEFINE_PROP_CHR("chardev[3]", SAUState, chr[3]),
};

static void sau_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sau_realize;
    // TODO: dc->vmsd

    // TODO: replace to preferrable APIs
    device_class_set_legacy_reset(dc, sau_reset);
    device_class_set_props(dc, sau_properties);
}

static const TypeInfo sau_info = {
    .name   = TYPE_RL78_SAU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SAUState),
    .instance_init = sau_init,
    .class_init = sau_class_init,
};

static void sau_register_types(void)
{
    type_register_static(&sau_info);
}
type_init(sau_register_types)
