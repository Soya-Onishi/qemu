#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqtest-single.h"
#include "hw/core/registerfields.h"


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

static void setup_sau(QTestState *s)
{
    qtest_system_reset(s);

    uint16_t sps = 0x0000;
    sps = FIELD_DP16(sps, SPS, PRS0, 8);
    sps = FIELD_DP16(sps, SPS, PRS1, 8);

    uint16_t smr = 0x0020;
    smr = FIELD_DP16(smr, SMR, MD0, 0);
    smr = FIELD_DP16(smr, SMR, MD1, 1);
    smr = FIELD_DP16(smr, SMR, SIS, 0);
    smr = FIELD_DP16(smr, SMR, STS, 0);
    smr = FIELD_DP16(smr, SMR, CCS, 0);
    smr = FIELD_DP16(smr, SMR, CKS, 0);

    uint16_t scr = 0x0004;
    scr = FIELD_DP16(scr, SCR, DLS, 3);
    scr = FIELD_DP16(scr, SCR, SLC, 1);
    scr = FIELD_DP16(scr, SCR, DIR, 1);
    scr = FIELD_DP16(scr, SCR, PTC, 0);
    scr = FIELD_DP16(scr, SCR, EOC, 0);
    scr = FIELD_DP16(scr, SCR, CKP, 0);
    scr = FIELD_DP16(scr, SCR, DAP, 0);
    scr = FIELD_DP16(scr, SCR, RXE, 0);
    scr = FIELD_DP16(scr, SCR, TXE, 1);

    uint16_t soe = 0x0000;
    soe = FIELD_DP16(soe, SOE, SOE0, 1);

    qtest_writew(s, A_SPS0, sps);
    qtest_writew(s, A_SMR00, smr);
    qtest_writew(s, A_SCR00, scr);
    qtest_writew(s, A_SDR00, 51 << 9);
    qtest_writew(s, A_SOE0, soe);
}

static void test_rl78_sau_send_byte(void)
{
    QTestState *s = qtest_init("-M qtest -nographic");

    setup_sau(s);
    uint16_t ss = 0x0000;
    ss = FIELD_DP16(ss, SS, SS0, 1);
    qtest_writew(s, A_SS0, ss);
 
    // send byte
    qtest_writew(s, A_SDR00, 'a');
    qtest_clock_step_next(s);

    uint16_t ssr = qtest_readw(s, A_SSR00);
    g_assert_cmpuint(FIELD_EX16(ssr, SSR, TSF), ==, 0);

    // TODO: Check Transmit Data
}

static void test_rl78_sau_continuous_send_byte(void)
{
    QTestState *s = qtest_init("-M qtest -nographic");

    setup_sau(s);
    uint16_t ss = 0x0000;
    ss = FIELD_DP16(ss, SS, SS0, 1);
    qtest_writew(s, A_SS0, ss);

    // First byte is moved into a shift register immediately, so BFF bit is not asserted
    // Note: Actual MCU has delay to move data into a shift register, so BFF is asserted in a short period.
    qtest_writew(s, A_SDR00, 0x0000 | 'a');
    uint16_t ssr0 = qtest_readw(s, A_SSR00);
    g_assert_cmpuint(FIELD_EX16(ssr0, SSR, BFF), ==, 0);
    g_assert_cmpuint(FIELD_EX16(ssr0, SSR, OVF), ==, 0);

    qtest_writew(s, A_SDR00, 0x0000 | 'b');
    uint16_t ssr1 = qtest_readw(s, A_SSR00);
    g_assert_cmpuint(FIELD_EX16(ssr1, SSR, BFF), ==, 1);
    g_assert_cmpuint(FIELD_EX16(ssr1, SSR, OVF), ==, 0);

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

    // TODO: Check Transmit Data
}

int main(int argc, char **argv)
{
    int ret;
    
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rl78/sau/send_byte", test_rl78_sau_send_byte);
    qtest_add_func("/rl78/sau/continuous_send_byte", test_rl78_sau_continuous_send_byte);

    ret = g_test_run();

    qtest_end();

    return ret;
}
