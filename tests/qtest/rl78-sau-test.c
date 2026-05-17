#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqtest-single.h"
#include "hw/core/clock.h"
#include "hw/core/registerfields.h"
#include "hw/rl78/intercomm.h"

#define SMR(INDEX)          \
    FIELD(SMR##INDEX, MD0, 0, 1)   \
    FIELD(SMR##INDEX, MD1, 1, 2)   \
    FIELD(SMR##INDEX, SIS, 6, 1)  \
    FIELD(SMR##INDEX, STS, 8, 1)  \
    FIELD(SMR##INDEX, CCS, 14, 1) \
    FIELD(SMR##INDEX, CKS, 15, 1)

#define SCR(INDEX) \
    FIELD(SCR##INDEX, DLS, 0, 2) \
    FIELD(SCR##INDEX, SLC, 4, 2) \
    FIELD(SCR##INDEX, DIR, 7, 1) \
    FIELD(SCR##INDEX, PTC, 8, 2) \
    FIELD(SCR##INDEX, EOC, 10, 1) \
    FIELD(SCR##INDEX, CKP, 12, 1) \
    FIELD(SCR##INDEX, DAP, 13, 1) \
    FIELD(SCR##INDEX, RXE, 14, 1) \
    FIELD(SCR##INDEX, TXE, 15, 1)

#define SIR(INDEX)    \
    FIELD(SIR##INDEX, OVCT, 0, 1)   \
    FIELD(SIR##INDEX, PECT, 1, 1)   \
    FIELD(SIR##INDEX, FECT, 2, 1)   \

#define SSR(INDEX)            \
    FIELD(SSR##INDEX, OVF, 0, 1)    \
    FIELD(SSR##INDEX, PEF, 1, 1)    \
    FIELD(SSR##INDEX, FEF, 2, 1)    \
    FIELD(SSR##INDEX, BFF, 5, 1)    \
    FIELD(SSR##INDEX, TSF, 6, 1)

#define SS(INDEX)         \
    FIELD(SS##INDEX, SS0, 0, 1) \
    FIELD(SS##INDEX, SS1, 1, 1) \
    FIELD(SS##INDEX, SS2, 2, 1) \
    FIELD(SS##INDEX, SS3, 3, 1)

#define ST(INDEX)         \
    FIELD(ST##INDEX, ST0, 0, 1) \
    FIELD(ST##INDEX, ST1, 1, 1) \
    FIELD(ST##INDEX, ST2, 2, 1) \
    FIELD(ST##INDEX, ST3, 3, 1)

#define SE(INDEX) \
    FIELD(SE##INDEX, SE0, 0, 1) \
    FIELD(SE##INDEX, SE1, 1, 1) \
    FIELD(SE##INDEX, SE2, 2, 1) \
    FIELD(SE##INDEX, SE3, 3, 1)

#define SOE(INDEX) \
    FIELD(SOE##INDEX, SOE0, 0, 1) \
    FIELD(SOE##INDEX, SOE1, 1, 1) \
    FIELD(SOE##INDEX, SOE2, 2, 1) \
    FIELD(SOE##INDEX, SOE3, 3, 1)

#define SO(INDEX) \
    FIELD(SO##INDEX, SO0, 0, 1) \
    FIELD(SO##INDEX, SO1, 1, 1) \
    FIELD(SO##INDEX, SO2, 2, 1) \
    FIELD(SO##INDEX, SO3, 3, 1)

#define SOL(INDEX)    \
    FIELD(SOL##INDEX, SOL0, 0, 1) \
    FIELD(SOL##INDEX, SOL2, 2, 1) 

// SAU Unit 0 Registers
REG16(SPS0, 0xF0126)

REG16(SMR00, 0xF0110)
REG16(SMR01, 0xF0112)
REG16(SMR02, 0xF0114)
REG16(SMR03, 0xF0116)

REG16(SCR00, 0xF0118)
REG16(SCR01, 0xF011A)
REG16(SCR02, 0xF011C)
REG16(SCR03, 0xF011E)

REG16(SDR00, 0xFFF10)
REG16(SDR01, 0xFFF12)
REG16(SDR02, 0xFFF44)
REG16(SDR03, 0xFFF46)

REG16(SIR00, 0xF0108)
REG16(SIR01, 0xF010A)
REG16(SIR02, 0xF010C)
REG16(SIR03, 0xF010E)

REG16(SSR00, 0xF0100)
REG16(SSR01, 0xF0102)
REG16(SSR02, 0xF0104)
REG16(SSR03, 0xF0106)

REG16(SE0, 0xF0120)
REG16(SS0, 0xF0122)
REG16(ST0, 0xF0124)

REG16(SO0, 0xF0128)
REG16(SOE0, 0xF012A)
REG16(SOL0, 0xF0134)

// SAU Unit 1

// Register Fields
FIELD(SPS, PRS0, 0, 4);
FIELD(SPS, PRS1, 4, 4);

FIELD(SMR, MD0, 0, 1);
FIELD(SMR, MD1, 1, 2);
FIELD(SMR, SIS, 6, 1);
FIELD(SMR, STS, 8, 1);
FIELD(SMR, CCS, 14, 1);
FIELD(SMR, CKS, 15, 1);

FIELD(SCR, DLS, 0, 2);
FIELD(SCR, SLC, 4, 2);
FIELD(SCR, DIR, 7, 1);
FIELD(SCR, PTC, 8, 2);
FIELD(SCR, EOC, 10, 1);
FIELD(SCR, CKP, 12, 1);
FIELD(SCR, DAP, 13, 1);
FIELD(SCR, RXE, 14, 1);
FIELD(SCR, TXE, 15, 1);

FIELD(SSR, OVF, 0, 1);
FIELD(SSR, PEF, 1, 1);
FIELD(SSR, FEF, 2, 1);
FIELD(SSR, BFF, 5, 1);
FIELD(SSR, TSF, 6, 1);

FIELD(SS, SS0, 0, 1);
FIELD(SS, SS1, 1, 1);
FIELD(SS, SS2, 2, 1);
FIELD(SS, SS3, 3, 1);

FIELD(ST, ST0, 0, 1);
FIELD(ST, ST1, 1, 1);
FIELD(ST, ST2, 2, 1);
FIELD(ST, ST3, 3, 1);

FIELD(SE, SE0, 0, 1);
FIELD(SE, SE1, 1, 1);
FIELD(SE, SE2, 2, 1);
FIELD(SE, SE3, 3, 1);

FIELD(SOE, SOE0, 0, 1);
FIELD(SOE, SOE1, 1, 1);
FIELD(SOE, SOE2, 2, 1);
FIELD(SOE, SOE3, 3, 1);

FIELD(SO, SO0, 0, 1);
FIELD(SO, SO1, 1, 1);
FIELD(SO, SO2, 2, 1);
FIELD(SO, SO3, 3, 1);

FIELD(SOL, SOL0, 0, 1);
FIELD(SOL, SOL2, 2, 1);

// Interrupt Register
REG16(IF0, 0xFFFE0)
REG16(IF1, 0xFFFE2)
REG16(IF2, 0xFFFD0)
REG16(IF3, 0xFFFD2)

typedef enum {
    IRQ_STIF0,
    IRQ_SRIF0,
    IRQ_SREIF0,
    IRQ_STIF1,
    IRQ_SRIF1,
    IRQ_SREIF1,
    IRQ_STIF2,
    IRQ_SRIF2,
    IRQ_SREIF2,
    IRQ_STIF3,
    IRQ_SRIF3,
    IRQ_SREIF3,
} IRQSource;

static void setup_sau(QTestState *s)
{
    uint16_t sps = 0x0000;
    sps = FIELD_DP16(sps, SPS, PRS0, 8);
    sps = FIELD_DP16(sps, SPS, PRS1, 8);
 
    uint16_t soe = 0x0000;
    soe = FIELD_DP16(soe, SOE, SOE0, 1);

    qtest_writew(s, A_SPS0, sps);
    qtest_writew(s, A_SDR00, 51 << 9);
    qtest_writew(s, A_SOE0, soe);
}


static void setup_smr(QTestState *s, uint32_t addr, uint8_t ck, uint8_t irq_source) {
    uint16_t smr = 0x0020;
    smr = FIELD_DP16(smr, SMR, MD0, irq_source);
    smr = FIELD_DP16(smr, SMR, MD1, 1);
    smr = FIELD_DP16(smr, SMR, SIS, 0);
    smr = FIELD_DP16(smr, SMR, STS, 0);
    smr = FIELD_DP16(smr, SMR, CCS, 0);
    smr = FIELD_DP16(smr, SMR, CKS, ck);

    qtest_writew(s, addr, smr);
}

static void setup_scr(QTestState *s, uint32_t addr, bool is_tx, bool is_rx, uint8_t parity_type, uint8_t stopbits, uint8_t bitsize) {
    uint16_t scr = 0x0004;

    switch(bitsize) {
        case 7:
            scr = FIELD_DP16(scr, SCR, DLS, 2);
            break;
        case 8: 
            scr = FIELD_DP16(scr, SCR, DLS, 3);
            break;
        case 9:
            scr = FIELD_DP16(scr, SCR, DLS, 1);
            break;
        default:
            g_assert_not_reached();
    }

    scr = FIELD_DP16(scr, SCR, SLC, stopbits);
    scr = FIELD_DP16(scr, SCR, DIR, 1);
    scr = FIELD_DP16(scr, SCR, PTC, parity_type);
    scr = FIELD_DP16(scr, SCR, EOC, 0);
    scr = FIELD_DP16(scr, SCR, CKP, 0);
    scr = FIELD_DP16(scr, SCR, DAP, 0);
    scr = FIELD_DP16(scr, SCR, RXE, is_rx ? 1 : 0);
    scr = FIELD_DP16(scr, SCR, TXE, is_tx ? 1 : 0);

    qtest_writew(s, addr, scr);
}

static bool get_irq_flag(QTestState *s, const IRQSource source) {
    switch(source) {
        case IRQ_STIF0:
            return !!extract16(qtest_readw(s, A_IF0), 13, 1);
        case IRQ_SRIF0:
            return !!extract16(qtest_readw(s, A_IF1), 4, 1);
        case IRQ_SREIF0:
            return !!extract16(qtest_readw(s, A_IF0), 15, 1);
        case IRQ_STIF1:
            return !!extract16(qtest_readw(s, A_IF1), 0, 1);
        case IRQ_SRIF1:
            return !!extract16(qtest_readw(s, A_IF1), 1, 1);
        case IRQ_SREIF1:
            return !!extract16(qtest_readw(s, A_IF1), 2, 1);
        case IRQ_STIF2:
            return !!extract16(qtest_readw(s, A_IF0), 0, 1);
        case IRQ_SRIF2:
            return !!extract16(qtest_readw(s, A_IF0), 1, 1);
        case IRQ_SREIF2:
            return !!extract16(qtest_readw(s, A_IF0), 2, 1);
        case IRQ_STIF3:
            return !!extract16(qtest_readw(s, A_IF1), 12, 1);
        case IRQ_SRIF3:
            return !!extract16(qtest_readw(s, A_IF1), 13, 1);
        case IRQ_SREIF3:
            return !!extract16(qtest_readw(s, A_IF2), 13, 1);
    }

    return 0;
}

static void set_irq_flag_impl(QTestState *s, const uint32_t addr, const uint8_t flag, const uint8_t pos)
{
    uint16_t irq = qtest_readw(s, addr);
    irq = deposit32(irq, pos, 1, flag);
    qtest_writew(s, addr, irq);
}

static void set_irq_flag(QTestState *s, const IRQSource source, bool flag) {
    uint8_t value = flag ? 0x0001 : 0x0000;

    switch(source) {
        case IRQ_STIF0:
            set_irq_flag_impl(s, A_IF0, value, 13); break;
        case IRQ_SRIF0:
            set_irq_flag_impl(s, A_IF1, value, 4); break;
        case IRQ_SREIF0:
            set_irq_flag_impl(s, A_IF0, value, 15); break;
        case IRQ_STIF1:
            set_irq_flag_impl(s, A_IF1, value, 0); break;
        case IRQ_SRIF1:
            set_irq_flag_impl(s, A_IF1, value, 1); break;
        case IRQ_SREIF1:
            set_irq_flag_impl(s, A_IF1, value, 2); break;
        case IRQ_STIF2:
            set_irq_flag_impl(s, A_IF0, value, 0); break;
        case IRQ_SRIF2:
            set_irq_flag_impl(s, A_IF0, value, 1); break;
        case IRQ_SREIF2:
            set_irq_flag_impl(s, A_IF0, value, 2); break;
        case IRQ_STIF3:
            set_irq_flag_impl(s, A_IF1, value, 12); break;
        case IRQ_SRIF3:
            set_irq_flag_impl(s, A_IF1, value, 13); break;
        case IRQ_SREIF3:
            set_irq_flag_impl(s, A_IF2, value, 13); break;
    }
}

static void qom_set_int(QTestState *s, const char *path, const char *property, const int value) {
    qtest_qmp(s, 
        "{ 'execute': 'qom-set', "
        "  'arguments': { "
        "     'path': %s, "
        "     'property': %s, "
        "     'value': %d"
        "}}",
        path, property, value
    );
}

static void send_byte(QTestState *s, uint8_t channel, uint8_t parity,uint8_t stopbits, uint8_t data) {
    char* parity_path = g_strdup_printf("sau-rx[%d].parity", channel);
    char* stopbits_path = g_strdup_printf("sau-rx[%d].stopbits", channel);
    char* payload_path = g_strdup_printf("sau-rx[%d].payload", channel);

    qom_set_int(s, "/machine", parity_path, parity);
    qom_set_int(s, "/machine", stopbits_path, stopbits);
    qom_set_int(s, "/machine", payload_path, data);

    g_free(payload_path);
    g_free(stopbits_path);
    g_free(parity_path);
}

static void setup_ss(QTestState *s, uint8_t channel) {
    qtest_writew(s, A_SS0, 1 << channel);
}

static uint16_t get_payload(QTestState *s, uint8_t channel) {
    char* property = g_strdup_printf("sau-tx[%d].payload", channel);

    QDict *response = qtest_qmp(s, 
        "{ 'execute': 'qom-get', "
        "  'arguments': { "
        "    'path': '/machine', "
        "    'property': %s "
        "}}",
        property
    );

    g_free(property);
    return qdict_get_int(response, "return");
}

static uint8_t get_stopbits(QTestState *s, uint8_t channel) {
    char* property = g_strdup_printf("sau-tx[%d].stopbits", channel);

    QDict *response = qtest_qmp(s, 
        "{ 'execute': 'qom-get', "
        "  'arguments': { "
        "    'path': '/machine', "
        "    'property': %s "
        "}}",
        property
    );

    g_free(property);
    return qdict_get_int(response, "return");
}

static UartParity get_parity(QTestState *s, uint8_t channel) {
    char* property = g_strdup_printf("sau-tx[%d].parity", channel);

    QDict *response = qtest_qmp(s, 
        "{ 'execute': 'qom-get', "
        "  'arguments': { "
        "    'path': '/machine', "
        "    'property': %s "
        "}}",
        property
    );

    g_free(property);
    return (UartParity)qdict_get_int(response, "return");
}

static void validate_sau_tx(QTestState *s, uint8_t channel, uint16_t payload, uint8_t stopbits, UartParity parity) {
    g_assert_cmpuint(get_payload(s, channel), ==, payload);
    g_assert_cmpuint(get_stopbits(s, channel), ==, stopbits);
    g_assert_cmpuint(get_parity(s, channel), ==, parity);
}

static void test_rl78_sau_single_send_byte(void) 
{
    QTestState *s = qtest_init("-M qtest -nographic");
    qtest_system_reset(s);

    setup_smr(s, A_SMR00, 0, 0);
    setup_scr(s, A_SCR00, true, false, 0, 1, 8);
    setup_sau(s);
    setup_ss(s, 0);
 
    // send byte
    qtest_writew(s, A_SDR00, 'a');
    qtest_clock_step_next(s);

    uint16_t ssr = qtest_readw(s, A_SSR00);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, TSF), ==, 0);

    validate_sau_tx(s, 0, 'a', 1, UART_PARITY_NONE);
}

static void test_rl78_sau_continuous_send_byte(void)
{
    QTestState *s = qtest_init("-M qtest -nographic");
    qtest_system_reset(s);

    setup_smr(s, A_SMR00, 0, 1);
    setup_scr(s, A_SCR00, true, false, 0, 1, 8);
    setup_sau(s);
    setup_ss(s, 0);

    // First byte is moved into a shift register immediately, so BFF bit is not asserted
    // Note: Actual MCU has delay to move data into a shift register, so BFF is asserted in a short period.
    qtest_writew(s, A_SDR00, 0x0000 | 'a');
    uint16_t ssr0 = qtest_readw(s, A_SSR00);
    g_assert_cmpuint(FIELD_EX16(ssr0, SSR, BFF), ==, 0);
    g_assert_cmpuint(FIELD_EX16(ssr0, SSR, OVF), ==, 0);
    validate_sau_tx(s, 0, 'a', 1, UART_PARITY_NONE);

    qtest_writew(s, A_SDR00, 0x0000 | 'b');
    uint16_t ssr1 = qtest_readw(s, A_SSR00);
    g_assert_cmpuint(FIELD_EX16(ssr1, SSR, BFF), ==, 1);
    g_assert_cmpuint(FIELD_EX16(ssr1, SSR, OVF), ==, 0);
    // 'a' is still transmitted, so payload is 'a'
    validate_sau_tx(s, 0, 'a', 1, UART_PARITY_NONE);

    // Check for OVF bit to be asserted if SDR has valid data
    qtest_writew(s, A_SDR00, 0x0000 | 'c');
    uint16_t ssr2 = qtest_readw(s, A_SSR00);
    g_assert_cmpuint(FIELD_EX16(ssr2, SSR, BFF), ==, 1);
    g_assert_cmpuint(FIELD_EX16(ssr2, SSR, OVF), ==, 1);

    // SSR continues to leave OVF bit behind.
    qtest_clock_step_next(s);
    uint16_t ssr3 = qtest_readw(s, A_SSR00);
    g_assert_cmpuint(FIELD_EX16(ssr3, SSR, BFF), ==, 0);
    g_assert_cmpuint(FIELD_EX16(ssr3, SSR, OVF), ==, 1);
    validate_sau_tx(s, 0, 'c', 1, UART_PARITY_NONE);
}

static void test_rl78_sau_single_send_byte_irq(void)
{
    QTestState *s = qtest_init("-M qtest -nographic");

    setup_smr(s, A_SMR00, 0, 0);
    setup_scr(s, A_SCR00, true, false, 0, 1, 8);
    setup_sau(s);
    setup_ss(s, 0);
 
    // send byte
    qtest_writew(s, A_SDR00, 'a');

    // IRQ is asserted after transmission period.
    g_assert_cmpuint(get_irq_flag(s, IRQ_STIF0), ==, 0);
    qtest_clock_step_next(s);
    g_assert_cmpuint(get_irq_flag(s, IRQ_STIF0), ==, 1);

    // Clear IRQ flag
    set_irq_flag(s, IRQ_STIF0, false);
    g_assert_cmpuint(get_irq_flag(s, IRQ_STIF0), ==, 0);

    qtest_writew(s, A_SDR00, 'b');
    qtest_clock_step_next(s);
    g_assert_cmpuint(get_irq_flag(s, IRQ_STIF0), ==, 1);
}

static void test_rl78_sau_continueous_send_byte_irq(void)
{
    QTestState *s = qtest_init("-M qtest -nographic");
    qtest_system_reset(s);

    setup_smr(s, A_SMR00, 0, 1);
    setup_scr(s, A_SCR00, true, false, 0, 1, 8);
    setup_sau(s);
    setup_ss(s, 0);

    // In continuous mode, IRQ is asserted immediately if transmission is idle.
    qtest_writew(s, A_SDR00, 'a');
    g_assert_cmpuint(get_irq_flag(s, IRQ_STIF0), ==, 1);

    set_irq_flag(s, IRQ_STIF0, false);
    qtest_writew(s, A_SDR00, 'b');
    g_assert_cmpuint(get_irq_flag(s, IRQ_STIF0), ==, 0);

    qtest_clock_step_next(s);
    g_assert_cmpuint(get_irq_flag(s, IRQ_STIF0), ==, 1);
}

G_GNUC_UNUSED
static void test_rl78_sau_0stopbit_tx(void) 
{
    // TODO: implement this test
}

static void test_rl78_sau_2stopbit_tx(void) 
{
    QTestState *s = qtest_init("-M qtest -nographic");
    qtest_system_reset(s);

    setup_smr(s, A_SMR00, 0, 1);
    setup_scr(s, A_SCR00, true, false, 0, 2, 8);
    setup_sau(s);
    setup_ss(s, 0);

    qtest_writew(s, A_SDR00, 'a');
    validate_sau_tx(s, 0, 'a', 2, UART_PARITY_NONE) ;

    qtest_clock_step_next(s);
    qtest_writew(s, A_SDR00, 'b');
    validate_sau_tx(s, 0, 'b', 2, UART_PARITY_NONE) ;

}

static void test_rl78_sau_even_parity_tx(void) 
{
    QTestState *s = qtest_init("-M qtest -nographic");
    qtest_system_reset(s);

    setup_smr(s, A_SMR00, 0, 1);
    setup_scr(s, A_SCR00, true, false, 2, 1, 8);
    setup_sau(s);
    setup_ss(s, 0);

    qtest_writew(s, A_SDR00, 'a');
    validate_sau_tx(s, 0, 'a', 1, UART_PARITY_EVEN) ;

    qtest_clock_step_next(s);
    qtest_writew(s, A_SDR00, 'b');
    validate_sau_tx(s, 0, 'b', 1, UART_PARITY_EVEN) ;
}

static void test_rl78_sau_odd_parity_tx(void)
{
    QTestState *s = qtest_init("-M qtest -nographic");
    qtest_system_reset(s);

    setup_smr(s, A_SMR00, 0, 1);
    setup_scr(s, A_SCR00, true, false, 3, 1, 8);
    setup_sau(s);
    setup_ss(s, 0);

    qtest_writew(s, A_SDR00, 'a');
    validate_sau_tx(s, 0, 'a', 1, UART_PARITY_ODD) ;

    qtest_clock_step_next(s);
    qtest_writew(s, A_SDR00, 'b');
    validate_sau_tx(s, 0, 'b', 1, UART_PARITY_ODD) ;
}

G_GNUC_UNUSED
static void test_rl78_sau_zero_parity_tx(void)
{
    // TODO: implement this test
}

G_GNUC_UNUSED
static void test_rl78_sau_little_endian_tx(void)
{
    // TODO: implement this test
}

static void test_rl78_sau_7bit_data_tx(void)
{
    QTestState *s = qtest_init("-M qtest -nographic");
    qtest_system_reset(s);

    setup_smr(s, A_SMR00, 0, 1);
    setup_scr(s, A_SCR00, true, false, 0, 1, 7);
    setup_sau(s);
    setup_ss(s, 0);

    qtest_writew(s, A_SDR00, 0x0155);
    validate_sau_tx(s, 0, 0x55, 1, UART_PARITY_NONE) ;

    qtest_clock_step_next(s);
    qtest_writew(s, A_SDR00, 0x01AA);
    validate_sau_tx(s, 0, 0x2A, 1, UART_PARITY_NONE) ;
}

static void test_rl78_sau_8bit_data_tx(void)
{
    QTestState *s = qtest_init("-M qtest -nographic");
    qtest_system_reset(s);

    setup_smr(s, A_SMR00, 0, 1);
    setup_scr(s, A_SCR00, true, false, 0, 1, 8);
    setup_sau(s);
    setup_ss(s, 0);

    qtest_writew(s, A_SDR00, 0x0155);
    validate_sau_tx(s, 0, 0x055, 1, UART_PARITY_NONE) ;

    qtest_clock_step_next(s);
    qtest_writew(s, A_SDR00, 0x01AA);
    validate_sau_tx(s, 0, 0x0AA, 1, UART_PARITY_NONE) ;
}

static void test_rl78_sau_9bit_data_tx(void)
{
    QTestState *s = qtest_init("-M qtest -nographic");
    qtest_system_reset(s);

    setup_smr(s, A_SMR00, 0, 1);
    setup_scr(s, A_SCR00, true, false, 0, 1, 9);
    setup_sau(s);
    setup_ss(s, 0);

    qtest_writew(s, A_SDR00, 0x0155);
    validate_sau_tx(s, 0, 0x155, 1, UART_PARITY_NONE) ;

    qtest_clock_step_next(s);
    qtest_writew(s, A_SDR00, 0x01AA);
    validate_sau_tx(s, 0, 0x1AA, 1, UART_PARITY_NONE) ;
}


G_GNUC_UNUSED
static void test_rl78_sau_9bit_ignored(void)
{
    // DLSmn1 in SCR02/SCR03 is always 1. Test here.
    // TODO: implement this test
}

static void test_rl78_sau_ssr_single_mode_tx(void)
{
    uint16_t ssr;

    QTestState *s = qtest_init("-M qtest -nographic");
    qtest_system_reset(s);

    setup_smr(s, A_SMR00, 0, 0);
    setup_scr(s, A_SCR00, true, false, 0, 1, 8);
    setup_sau(s);
    setup_ss(s, 0);

    qtest_writew(s, A_SDR00, 'a');

    ssr = qtest_readw(s, A_SSR00);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, TSF), ==, 1);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, BFF), ==, 0);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, OVF), ==, 0);

    qtest_clock_step_next(s);
    ssr = qtest_readw(s, A_SSR00);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, TSF), ==, 0);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, BFF), ==, 0);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, OVF), ==, 0);

    // Even if transmittion is busy, BFF and OVF is not asserted in single mode.
    qtest_writew(s, A_SDR00, 'b');
    qtest_writew(s, A_SDR00, 'c');
    ssr = qtest_readw(s, A_SSR00);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, TSF), ==, 1);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, BFF), ==, 0);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, OVF), ==, 0);

    qtest_writew(s, A_SDR00, 'd');
    ssr = qtest_readw(s, A_SSR00);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, TSF), ==, 1);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, BFF), ==, 0);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, OVF), ==, 0);
}

static void test_rl78_sau_ssr_continuous_mode_tx(void)
{
    uint16_t ssr;

    QTestState *s = qtest_init("-M qtest -nographic");
    qtest_system_reset(s);

    setup_smr(s, A_SMR00, 0, 1);
    setup_scr(s, A_SCR00, true, false, 0, 1, 8);
    setup_sau(s);
    setup_ss(s, 0);

    qtest_writew(s, A_SDR00, 'a');

    ssr = qtest_readw(s, A_SSR00);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, TSF), ==, 1);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, BFF), ==, 0);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, OVF), ==, 0);

    qtest_clock_step_next(s);
    ssr = qtest_readw(s, A_SSR00);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, TSF), ==, 0);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, BFF), ==, 0);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, OVF), ==, 0);

    qtest_writew(s, A_SDR00, 'b');
    qtest_writew(s, A_SDR00, 'c');
    ssr = qtest_readw(s, A_SSR00);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, TSF), ==, 1);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, BFF), ==, 1);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, OVF), ==, 0);

    qtest_writew(s, A_SDR00, 'd');
    ssr = qtest_readw(s, A_SSR00);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, TSF), ==, 1);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, BFF), ==, 1);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, OVF), ==, 1);
}

static void test_rl78_sau_clock_38400bps(void)
{
    // TODO: Currently, fCLK is expected as 32MHz.
    // If this frequency is changed or arbitrary, fix here.

    QTestState *s = qtest_init("-M qtest -nographic");
    qtest_system_reset(s);

    setup_scr(s, A_SCR00, true, false, 0, 1, 8);
    setup_sau(s);
   
    // SPS: CK00 = 8MHz, CK01 = 4MHz
    uint16_t sps = 0x0000;
    sps = FIELD_DP16(sps, SPS, PRS0, 2);
    sps = FIELD_DP16(sps, SPS, PRS1, 3);
    qtest_writew(s, A_SPS0, sps);

    // Use CK00
    // 38400bps = 8Mhz / (SDR[15:9] + 1) / 2
    // SDR[15:9] = (8 * 1000 * 1000) / 38400 / 2 - 1 = 103.xxxx = 103
    setup_smr(s, A_SMR00, 0, 0);
    qtest_writew(s, A_SDR00, 103 << 9);
    setup_ss(s, 0);

    set_irq_flag(s, IRQ_STIF0, false);
    qtest_writew(s, A_SDR00, 0x0155);

    // 8MHz / (103 + 1) / 2 = 38461bps
    const uint64_t period = (CLOCK_PERIOD_FROM_HZ(38461) * 10) >> 32;
    qtest_clock_step(s, period - 1);
    g_assert_cmpuint(get_irq_flag(s, IRQ_STIF0), ==, 0);

    qtest_clock_step(s, 1);
    g_assert_cmpuint(get_irq_flag(s, IRQ_STIF0), ==, 1);
}

static void test_rl78_sau_clock_57600bps(void)
{
    // TODO: Currently, fCLK is expected as 32MHz.
    // If this frequency is changed or arbitrary, fix here.

    QTestState *s = qtest_init("-M qtest -nographic");
    qtest_system_reset(s);

    setup_scr(s, A_SCR00, true, false, 0, 1, 8);
    setup_sau(s);
   
    // SPS: CK00 = 8MHz, CK01 = 16MHz
    uint16_t sps = 0x0000;
    sps = FIELD_DP16(sps, SPS, PRS0, 2);
    sps = FIELD_DP16(sps, SPS, PRS1, 1);
    qtest_writew(s, A_SPS0, sps);

    // Use CK01
    // 115200bps = 16Mhz / (SDR[15:9] + 1) / 2
    // SDR[15:9] = (16 * 1000 * 1000) / 115200 / 2 - 1 = 68.xxxx = 68
    setup_smr(s, A_SMR00, 1, 0);
    qtest_writew(s, A_SDR00, 68 << 9);
    setup_ss(s, 0);

    set_irq_flag(s, IRQ_STIF0, false);
    qtest_writew(s, A_SDR00, 0x0155);

    // 16MHz / (68 + 1) / 2 = 115942bps
    const uint64_t period = (CLOCK_PERIOD_FROM_HZ(115942) * 10) >> 32;
    qtest_clock_step(s, period - 1);
    g_assert_cmpuint(get_irq_flag(s, IRQ_STIF0), ==, 0);

    qtest_clock_step(s, 1);
    g_assert_cmpuint(get_irq_flag(s, IRQ_STIF0), ==, 1);
}

static void test_rl78_sau_receive_byte(void) 
{
    QTestState *s = qtest_init("-M qtest -nographic");
    qtest_system_reset(s);

    setup_smr(s, A_SMR00, 0, 0);
    setup_smr(s, A_SMR01, 0, 0);
    setup_scr(s, A_SCR01, false, true, 0, 1, 8);
    setup_sau(s);
    setup_ss(s, 1);

    g_assert_cmpuint(qtest_readb(s, A_SDR01), ==, 0);
    g_assert_cmpuint(get_irq_flag(s, IRQ_SRIF0), ==, 0);

    send_byte(s, 0, 0, 1, 'a');
    g_assert_cmpuint(qtest_readb(s, A_SDR01), ==, 'a');
    g_assert_cmpuint(get_irq_flag(s, IRQ_SRIF0), ==, 1);

    set_irq_flag(s, IRQ_SRIF0, false);
    send_byte(s, 0, 0, 1, 'b');
    g_assert_cmpuint(qtest_readb(s, A_SDR01), ==, 'b');
    g_assert_cmpuint(get_irq_flag(s, IRQ_SRIF0), ==, 1);

}

static void test_rl78_sau_receive_multiple_bytes(void)
{
    const uint64_t period = (CLOCK_PERIOD_FROM_HZ(38461) * 10) >> 32;

    QTestState *s = qtest_init("-M qtest -nographic");
    qtest_system_reset(s);

    setup_smr(s, A_SMR00, 0, 0);
    setup_smr(s, A_SMR01, 0, 0);
    setup_scr(s, A_SCR01, false, true, 0, 1, 8);
    setup_sau(s);

    // SPS: CK00 = 8MHz, CK01 = 16MHz
    uint16_t sps = 0x0000;
    sps = FIELD_DP16(sps, SPS, PRS0, 2);
    sps = FIELD_DP16(sps, SPS, PRS1, 1);
    qtest_writew(s, A_SPS0, sps);

    // Use CK00
    // 38400bps = 8Mhz / (SDR[15:9] + 1) / 2
    // SDR[15:9] = (8 * 1000 * 1000) / 38400 / 2 - 1 = 103.xxxx = 103
    qtest_writew(s, A_SDR00, 103 << 9);
    qtest_writew(s, A_SDR01, 103 << 9);
    setup_ss(s, 0);
    setup_ss(s, 1);

    g_assert_cmpuint(FIELD_EX16(qtest_readb(s, A_SSR01), SSR, TSF), ==, 0);
    g_assert_cmpuint(FIELD_EX16(qtest_readb(s, A_SSR01), SSR, BFF), ==, 0);

    send_byte(s, 0, 0, 1, 'a');
    g_assert_cmpuint(FIELD_EX16(qtest_readb(s, A_SSR01), SSR, TSF), ==, 0);
    g_assert_cmpuint(FIELD_EX16(qtest_readb(s, A_SSR01), SSR, BFF), ==, 1);

    send_byte(s, 0, 0, 1, 'b');
    g_assert_cmpuint(FIELD_EX16(qtest_readb(s, A_SSR01), SSR, TSF), ==, 1);
    g_assert_cmpuint(FIELD_EX16(qtest_readb(s, A_SSR01), SSR, BFF), ==, 1);

    g_assert_cmpuint(qtest_readb(s, A_SDR01), ==, 'a');
    g_assert_cmpuint(FIELD_EX16(qtest_readb(s, A_SSR01), SSR, TSF), ==, 1);
    g_assert_cmpuint(FIELD_EX16(qtest_readb(s, A_SSR01), SSR, BFF), ==, 0);

    qtest_clock_step(s, period - 1);
    g_assert_cmpuint(qtest_readb(s, A_SDR01), ==, 'a');
    g_assert_cmpuint(FIELD_EX16(qtest_readb(s, A_SSR01), SSR, TSF), ==, 1);

    qtest_clock_step(s, 1);
    g_assert_cmpuint(FIELD_EX16(qtest_readb(s, A_SSR01), SSR, TSF), ==, 0);
    g_assert_cmpuint(FIELD_EX16(qtest_readb(s, A_SSR01), SSR, BFF), ==, 1);

    g_assert_cmpuint(qtest_readb(s, A_SDR01), ==, 'b');
    g_assert_cmpuint(FIELD_EX16(qtest_readb(s, A_SSR01), SSR, TSF), ==, 0);
    g_assert_cmpuint(FIELD_EX16(qtest_readb(s, A_SSR01), SSR, BFF), ==, 0);

    qtest_clock_step_next(s);

    send_byte(s, 0, 0, 1, 'c');
    send_byte(s, 0, 0, 1, 'd');
    qtest_clock_step_next(s);    
    g_assert_cmpuint(FIELD_EX16(qtest_readb(s, A_SSR01), SSR, OVF), ==, 1);
}


G_GNUC_UNUSED
static void test_rl78_sau_ssr_rx(void)
{
    // TODO: implement this test
}

int main(int argc, char **argv)
{
    int ret;
    
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rl78/sau/single_send_byte", test_rl78_sau_single_send_byte);
    qtest_add_func("/rl78/sau/continuous_send_byte", test_rl78_sau_continuous_send_byte);
    qtest_add_func("/rl78/sau/single_send_byte_irq", test_rl78_sau_single_send_byte_irq);

    qtest_add_func("/rl78/sau/2stopbit_tx", test_rl78_sau_2stopbit_tx);
    qtest_add_func("/rl78/sau/even_parity_tx", test_rl78_sau_even_parity_tx);
    qtest_add_func("/rl78/sau/odd_parity_tx", test_rl78_sau_odd_parity_tx);

    qtest_add_func("/rl78/sau/7bit_data_tx", test_rl78_sau_7bit_data_tx);
    qtest_add_func("/rl78/sau/8bit_data_tx", test_rl78_sau_8bit_data_tx);
    qtest_add_func("/rl78/sau/9bit_data_tx", test_rl78_sau_9bit_data_tx);

    qtest_add_func("/rl78/sau/clock_38400bps", test_rl78_sau_clock_38400bps);
    qtest_add_func("/rl78/sau/clock_57600bps", test_rl78_sau_clock_57600bps);

    qtest_add_func("/rl78/sau/ssr_single_mode_tx", test_rl78_sau_ssr_single_mode_tx);
    qtest_add_func("/rl78/sau/ssr_continuous_mode_tx", test_rl78_sau_ssr_continuous_mode_tx);

    qtest_add_func("/rl78/sau/continuous_send_byte_irq", test_rl78_sau_continueous_send_byte_irq);

    qtest_add_func("/rl78/sau/receive_byte", test_rl78_sau_receive_byte);
    qtest_add_func("/rl78/sau/receive_multiple_bytes", test_rl78_sau_receive_multiple_bytes);
    ret = g_test_run();

    qtest_end();

    return ret;
}
