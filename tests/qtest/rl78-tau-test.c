#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqtest-single.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "hw/core/registerfields.h"

#define TAU_TDR(m) (m == 0 ? 0xFFF18 : m == 1 ? 0xFFF1A : 0xFFF60 + ((m)*2))
#define TAU_TCR(m) (0xF0180 + ((m) * 2))
#define TAU_TPS (0xF01B6)
#define TAU_TMR(m) (0xF0190 + ((m) * 2))
#define TAU_TSR(m) (0xF01A0 + ((m) * 2))
#define TAU_TE (0xF01B0)
#define TAU_TS (0xF01B2)
#define TAU_TT (0xF01B4)
#define TAU_TIS0 (0xF0074)
#define TAU_TIS1 (0xF0075)
#define TAU_TOE (0xF01BA)
#define TAU_TO (0xF01B8)
#define TAU_TOL (0xF01BC)
#define TAU_TOM (0xF01BE)

FIELD(TPS, PRS0, 0, 4)
FIELD(TPS, PRS1, 4, 4)
FIELD(TPS, PRS2, 8, 2)
FIELD(TPS, PRS3, 12, 2)

FIELD(TMR, MD0, 0, 1)
FIELD(TMR, MD1, 1, 3)
FIELD(TMR, CIS, 6, 2)
FIELD(TMR, STS, 8, 3)
FIELD(TMR, MASTER, 11, 1)
FIELD(TMR, SPLIT, 11, 1)
FIELD(TMR, CCS, 12, 1)
FIELD(TMR, CKS, 14, 2)

static void setup_interval_timer(QTestState *s, const uint8_t channel)
{
    qtest_writew(s, TAU_TOE, 0x0000);
    qtest_writew(s, TAU_TO, 0x0000);
    qtest_writew(s, TAU_TOL, 0x0000);
    qtest_writeb(s, TAU_TIS0, 0x00);
    qtest_writeb(s, TAU_TIS1, 0x00);
}

static bool get_tmr_iflag(QTestState *s, const uint8_t channel)
{
    switch(channel) {
        case 0:
            return !!(qtest_readw(s, 0xFFFE0) & 0x4000);
        case 1:
            return !!(qtest_readw(s, 0xFFFE2) & 0x0020);
        case 2:
            return !!(qtest_readw(s, 0xFFFE2) & 0x0040);
        case 3:
            return !!(qtest_readw(s, 0xFFFE2) & 0x0080);
        default:
            g_assert_not_reached();
            return false;
    }
}

static bool get_high_tmr_iflag(QTestState *s, const uint8_t channel)
{
    switch(channel) {
        case 1:
            return !!(qtest_readw(s, 0xFFFE0) & 0x8000);
        case 3:
            return !!(qtest_readw(s, 0xFFFE2) & 0x0004);
        default:
            g_assert_not_reached();
            return false;
    }
}

static void set_tmr_iflag(QTestState *s, const uint8_t channel, const bool bit)
{
    uint16_t value = 0;

    switch(channel) {
        case 0:
            value = qtest_readw(s, 0xFFFE0);
            value = deposit32(value, 14, 1, bit);
            qtest_writew(s, 0xFFFE0, value);
            break;
        case 1:
            value = qtest_readw(s, 0xFFFE2);
            value = deposit32(value, 5, 1, bit);
            qtest_writew(s, 0xFFFE2, value);
            break;
        case 2:
            value = qtest_readw(s, 0xFFFE2);
            value = deposit32(value, 6, 1, bit);
            qtest_writew(s, 0xFFFE2, value);
            break;
        case 3:
            value = qtest_readw(s, 0xFFFE2);
            value = deposit32(value, 7, 1, bit);
            qtest_writew(s, 0xFFFE2, value);
            break;
        default:
            g_assert_not_reached();
    }
}

G_GNUC_UNUSED
static void set_high_tmr_iflag(QTestState *s, const uint8_t channel, const bool bit)
{
    uint16_t value = 0;

    switch(channel) {
        case 1:
            value = qtest_readw(s, 0xFFFE0);
            value = deposit32(value, 15, 1, bit);
            qtest_writew(s, 0xFFFE0, value);
            break;
        case 3:
            value = qtest_readw(s, 0xFFFE2);
            value = deposit32(value, 2, 1, bit);
            qtest_writew(s, 0xFFFE2, value);
            break;
        default:
            g_assert_not_reached();
    }
}

static void interval_timer_basic_usage_impl(const uint8_t channel, const uint8_t *prs, const uint8_t ck, const uint16_t tdr, const uint64_t interval_ns)
{
    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);

    setup_interval_timer(s, channel);

    qtest_writew(s, TAU_TDR(channel), tdr);

    uint16_t tps = 0x0000;
    tps = FIELD_DP16(tps, TPS, PRS0, prs[0]);
    tps = FIELD_DP16(tps, TPS, PRS1, prs[1]);
    tps = FIELD_DP16(tps, TPS, PRS2, prs[2]);
    tps = FIELD_DP16(tps, TPS, PRS3, prs[3]);
    qtest_writew(s, TAU_TPS, tps);

    uint16_t tmr = 0x0000;
    switch(ck) {
        case 0: tmr = FIELD_DP16(tmr, TMR, CKS, 0); break;
        case 1: tmr = FIELD_DP16(tmr, TMR, CKS, 2); break;
        case 2: tmr = FIELD_DP16(tmr, TMR, CKS, 1); break;
        case 3: tmr = FIELD_DP16(tmr, TMR, CKS, 3); break;
    }
    tmr = FIELD_DP16(tmr, TMR, MD1, 0);     // interval timer mode
    tmr = FIELD_DP16(tmr, TMR, MD0, 0);     // no irq at beginning
    qtest_writew(s, TAU_TMR(channel), tmr);

    qtest_writew(s, TAU_TS, 1  << channel);
    
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(channel)), ==, tdr); 

    // step half interval
    qtest_clock_step(s, interval_ns / 2);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(channel)), ==, tdr/2);
    g_assert_cmpuint(get_tmr_iflag(s, channel), ==, 0);

    // interval timer should be up
    qtest_clock_step(s, interval_ns / 2);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(channel)), ==, tdr);
    g_assert_cmpuint(get_tmr_iflag(s, channel), ==, 1);
    set_tmr_iflag(s, channel, false);

    // step 50us
    qtest_clock_step(s, interval_ns / 2);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(channel)), ==, tdr/2);
    g_assert_cmpuint(get_tmr_iflag(s, channel), ==, 0);
}

static void test_rl78_tau_interval_timer_use_ck0(void)
{
    const uint8_t prs[4] = { 1,2,3, 3, };
    interval_timer_basic_usage_impl(0, prs, 0, 7999, 500*1000);
}

static void test_rl78_tau_interval_timer_use_ck1(void)
{
    const uint8_t prs[4] = { 1,2,3, 3, };
    interval_timer_basic_usage_impl(0, prs, 1, 1999, 250*1000);
}

static void test_rl78_tau_interval_timer_use_ck2_0(void)
{
    const uint8_t prs[4] = { 1,2,0, 3, };
    interval_timer_basic_usage_impl(1, prs, 2, 15999, 1000*1000);
}

static void test_rl78_tau_interval_timer_use_ck2_1(void)
{
    const uint8_t prs[4] = { 1,2,1, 3, };
    interval_timer_basic_usage_impl(1, prs, 2, 7999, 1000*1000);
}

static void test_rl78_tau_interval_timer_use_ck2_2(void)
{
    const uint8_t prs[4] = { 1,2,2, 3, };
    interval_timer_basic_usage_impl(1, prs, 2, 1999, 1000*1000);
}

static void test_rl78_tau_interval_timer_use_ck2_3(void)
{
    const uint8_t prs[4] = { 1,2,3, 3, };
    interval_timer_basic_usage_impl(1, prs, 2, 499, 1000*1000);
}

static void test_rl78_tau_interval_timer_use_ck3_0(void)
{
    const uint8_t prs[4] = { 1,2,3, 0, };
    interval_timer_basic_usage_impl(3, prs, 3, 1249, 10*1000*1000);
}

static void test_rl78_tau_interval_timer_use_ck3_1(void)
{
    const uint8_t prs[4] = { 1,2,3, 1, };
    interval_timer_basic_usage_impl(3, prs, 3, 15624, 500*1000*1000);
}

static void test_rl78_tau_interval_timer_use_ck3_2(void)
{
    const uint8_t prs[4] = { 1,2,3, 2, };
    interval_timer_basic_usage_impl(3, prs, 3, 15624, 2*1000*1000*1000);
}

static void test_rl78_tau_interval_timer_use_ck3_3(void)
{
    const uint8_t prs[4] = { 1,2,3, 3, };
    interval_timer_basic_usage_impl(3, prs, 3, 31249, 16*1000*1000*1000UL);
}

static void test_rl78_tau_interval_timer_use_slowest_division(void)
{
    const uint8_t prs[4] = { 15,2,3, 3, };
    interval_timer_basic_usage_impl(0, prs, 0, 15624, 16*1000*1000*1000UL);
}

static void rl78_tau_interval_timer_using_ck2(const uint16_t tps, const uint32_t interval)
{
    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);

    setup_interval_timer(s, 0);

    qtest_writew(s, TAU_TDR(1), 3199);
    qtest_writew(s, TAU_TPS, 0x0010 | (tps << 8));

    qtest_writew(s, TAU_TMR(1), 0x4000);
    qtest_writew(s, TAU_TS, 0x0002);
    
    qtest_clock_step(s, 1);
 
    qtest_clock_step(s, interval / 2);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, 1599);
    g_assert_cmpuint(get_tmr_iflag(s, 1), ==, 0);

    qtest_clock_step(s, interval / 2);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, 3199);
    g_assert_cmpuint(get_tmr_iflag(s, 1), ==, 1);
}

static void test_rl78_tau_interval_timer_using_ck2_0(void)
{
    rl78_tau_interval_timer_using_ck2(0x00, 200*1000);
}

static void test_rl78_tau_interval_timer_using_ck2_1(void)
{
    rl78_tau_interval_timer_using_ck2(0x01, 400*1000);
}

static void test_rl78_tau_interval_timer_using_ck2_2(void)
{
    rl78_tau_interval_timer_using_ck2(0x02, 1600*1000);
}

static void test_rl78_tau_interval_timer_using_ck2_3(void)
{
    rl78_tau_interval_timer_using_ck2(0x03, 6400*1000);
}

static void rl78_tau_interval_timer_using_ck3(const uint16_t tps, const uint32_t interval)
{
    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);

    setup_interval_timer(s, 0);

    qtest_writew(s, TAU_TDR(1), 3199);
    qtest_writew(s, TAU_TPS, 0x0010 | (tps << 12));

    qtest_writew(s, TAU_TMR(1), 0xC000);
    qtest_writew(s, TAU_TS, 0x0002);
    
    qtest_clock_step(s, 1);
 
    qtest_clock_step(s, interval / 2);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, 1599);
    g_assert_cmpuint(get_tmr_iflag(s, 1), ==, 0);

    qtest_clock_step(s, interval / 2);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, 3199);
    g_assert_cmpuint(get_tmr_iflag(s, 1), ==, 1);
}

static void test_rl78_tau_interval_timer_using_ck3_0(void)
{
    rl78_tau_interval_timer_using_ck3(0x00, 256*100*1000);
}

static void test_rl78_tau_interval_timer_using_ck3_1(void)
{
    rl78_tau_interval_timer_using_ck3(0x01, 1024*100*1000);
}

static void test_rl78_tau_interval_timer_using_ck3_2(void)
{
    rl78_tau_interval_timer_using_ck3(0x02, 4096*100*1000);
}

static void test_rl78_tau_interval_timer_using_ck3_3(void)
{
    rl78_tau_interval_timer_using_ck3(0x03, 16384*100*1000);
}

static void test_rl78_tau_interval_timer_stop_timer(void) 
{
    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);

    setup_interval_timer(s, 0);

    // 100us interval
    qtest_writew(s, TAU_TDR(0), 3199);
    qtest_writew(s, TAU_TPS, 0x0010);

    qtest_writew(s, TAU_TMR(0), 0x0000);
    qtest_writew(s, TAU_TS, 0x0001);
     
    // step 50us
    qtest_clock_step(s, 50*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(0)), ==, 1599);

    // stop timer
    qtest_writew(s, TAU_TT, 0x0001);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(0)), ==, 1599);
    
    // step 50us
    qtest_clock_step(s, 50*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(0)), ==, 1599);
    g_assert_cmpuint(get_tmr_iflag(s, 0), ==, 0);

    // restart timer
    qtest_writew(s, TAU_TS, 0x0001);
    qtest_clock_step(s, 1);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(0)), ==, 3199);
    g_assert_cmpuint(get_tmr_iflag(s, 0), ==, 0);

    qtest_clock_step(s, 50*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(0)), ==, 1599);
    g_assert_cmpuint(get_tmr_iflag(s, 0), ==, 0);
}

static uint16_t tcr_8bit(const uint8_t hi, const uint8_t lo) 
{
    return ((uint16_t)hi << 8) | (uint16_t)lo;
}

static void interval_timer_8bit_timer_impl(const uint8_t channel, bool use_loside, const uint8_t *prs, const uint8_t ck, const uint16_t tdr, const uint64_t interval_ns)
{
    const uint16_t trigger = use_loside ? (1 << channel) : (1 << (channel + 8));
    const uint32_t tdraddr = use_loside ? TAU_TDR(channel) : TAU_TDR(channel) + 1;
    const uint32_t tcraddr = TAU_TCR(channel);
    const uint16_t expect0 = use_loside ? tdr : (tdr << 8);
    const uint16_t expect1 = use_loside ? tdr/2 : ((tdr/2) << 8);
    bool (*get_iflag)(QTestState *s, const uint8_t channel) = use_loside ? get_tmr_iflag : get_high_tmr_iflag;

    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);

    setup_interval_timer(s, 0);

    uint16_t tps = 0x0000;
    tps = FIELD_DP16(tps, TPS, PRS0, prs[0]);
    tps = FIELD_DP16(tps, TPS, PRS1, prs[1]);
    tps = FIELD_DP16(tps, TPS, PRS2, prs[2]);
    tps = FIELD_DP16(tps, TPS, PRS3, prs[3]);
    qtest_writew(s, TAU_TPS, tps);

    uint16_t tmr = 0x0000;
    switch(ck) {
        case 0: tmr = FIELD_DP16(tmr, TMR, CKS, 0); break;
        case 1: tmr = FIELD_DP16(tmr, TMR, CKS, 2); break;
        case 2: tmr = FIELD_DP16(tmr, TMR, CKS, 1); break;
        case 3: tmr = FIELD_DP16(tmr, TMR, CKS, 3); break;
    }
    tmr = FIELD_DP16(tmr, TMR, SPLIT, 1);   // use 8bit timer
    tmr = FIELD_DP16(tmr, TMR, MD1, 0);     // interval timer mode
    tmr = FIELD_DP16(tmr, TMR, MD0, 0);     // no irq at beginning
    qtest_writew(s, TAU_TMR(channel), tmr); 

    qtest_writeb(s, tdraddr, tdr);

    qtest_writew(s, TAU_TS, trigger);

    g_assert_cmpuint(qtest_readw(s, tcraddr), ==, expect0);

    // step half interval
    qtest_clock_step(s, interval_ns / 2);
    g_assert_cmpuint(qtest_readw(s, tcraddr), ==, expect1);

    // stop timer
    qtest_writew(s, TAU_TT, trigger);
    g_assert_cmpuint(qtest_readw(s, tcraddr), ==, expect1);
    
    // TCR value is same even if stepping half interval
    qtest_clock_step(s, interval_ns / 2);
    g_assert_cmpuint(qtest_readw(s, tcraddr), ==, expect1);
    g_assert_cmpuint(get_iflag(s, channel), ==, false);

    // restart timer
    qtest_writew(s, TAU_TS, trigger);
    g_assert_cmpuint(qtest_readw(s, tcraddr), ==, expect0);
    g_assert_cmpuint(get_iflag(s, channel), ==, false);

    qtest_clock_step(s, interval_ns / 2);
    g_assert_cmpuint(qtest_readw(s, tcraddr), ==, expect1);
    g_assert_cmpuint(get_iflag(s, channel), ==, false);

    // timer should be up
    qtest_clock_step(s, interval_ns / 2);
    g_assert_cmpuint(qtest_readw(s, tcraddr), ==, expect0);
    g_assert_cmpuint(get_iflag(s, channel), ==, true);
}

static void test_rl78_tau_interval_timer_8bit_timer_ch1_lo(void) {
    const uint8_t prs[4] = { 0,15,3, 3, };
    interval_timer_8bit_timer_impl(1, true, prs, 0, 159, 5*1000UL);
}

static void test_rl78_tau_interval_timer_8bit_timer_ch1_hi(void) {
    const uint8_t prs[4] = { 0,15,3, 3, };
    interval_timer_8bit_timer_impl(1, false, prs, 1, 124, 128*1000*1000UL);
}

static void test_rl78_tau_interval_timer_8bit_timer_ch3_lo(void) {
    const uint8_t prs[4] = { 1,15,0, 3, };
    interval_timer_8bit_timer_impl(3, true, prs, 2, 79, 5*1000UL);
}

static void test_rl78_tau_interval_timer_8bit_timer_ch3_hi(void) {
    const uint8_t prs[4] = { 1,15,0, 3, };
    interval_timer_8bit_timer_impl(3, false, prs, 3, 249, 128*1000*1000UL);
}

static void test_rl78_tau_interval_timer_8bit_timer_simultaneous(void)
{
    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);

    setup_interval_timer(s, 0);

    uint16_t tps = 0x0000;
    tps = FIELD_DP16(tps, TPS, PRS0, 0);
    tps = FIELD_DP16(tps, TPS, PRS1, 1);
    tps = FIELD_DP16(tps, TPS, PRS2, 2);
    tps = FIELD_DP16(tps, TPS, PRS3, 3);
    qtest_writew(s, TAU_TPS, tps);

    uint16_t tmr = 0x0000;
    tmr = FIELD_DP16(tmr, TMR, CKS, 0);
    tmr = FIELD_DP16(tmr, TMR, SPLIT, 1);   // use 8bit timer
    tmr = FIELD_DP16(tmr, TMR, MD1, 0);     // interval timer mode
    tmr = FIELD_DP16(tmr, TMR, MD0, 0);     // no irq at beginning
    qtest_writew(s, TAU_TMR(1), tmr); 

    qtest_writew(s, TAU_TDR(1), tcr_8bit(160-1, 240-1));

    qtest_writew(s, TAU_TS, 0x0202);

    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(160-1, 240-1));

    // step half interval
    qtest_clock_step(s, 2500);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(80-1, 160-1));

    // stop timer
    qtest_writew(s, TAU_TT, 0x0202);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(80-1, 160-1));
    
    // TCR value is same even if stepping half interval
    qtest_clock_step(s, 2500);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(80-1, 160-1));
    g_assert_cmpuint(get_tmr_iflag(s, 1), ==, false);
    g_assert_cmpuint(get_high_tmr_iflag(s, 1), ==, false);

    // restart timer
    qtest_writew(s, TAU_TS, 0x0202);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(160-1, 240-1));
    g_assert_cmpuint(get_tmr_iflag(s, 1), ==, false);
    g_assert_cmpuint(get_high_tmr_iflag(s, 1), ==, false);

    qtest_clock_step(s, 2500);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(80-1, 160-1));
    g_assert_cmpuint(get_tmr_iflag(s, 1), ==, false);
    g_assert_cmpuint(get_high_tmr_iflag(s, 1), ==, false);

    // high timer should be up
    qtest_clock_step(s, 2500);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(160-1, 80-1));
    g_assert_cmpuint(get_tmr_iflag(s, 1), ==, false);
    g_assert_cmpuint(get_high_tmr_iflag(s, 1), ==, true);
    set_tmr_iflag(s, 1, false);
    set_high_tmr_iflag(s, 1, false);

    // low timer should be up
    qtest_clock_step(s, 2500);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(80-1, 240-1));
    g_assert_cmpuint(get_tmr_iflag(s, 1), ==, true);
    g_assert_cmpuint(get_high_tmr_iflag(s, 1), ==, false);
    set_tmr_iflag(s, 1, false);
    set_high_tmr_iflag(s, 1, false);

    // stop high side only
    qtest_writew(s, TAU_TT, 0x0200);
    qtest_clock_step(s, 2500);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(80-1, 160-1));

    // stop low side also
    qtest_writew(s, TAU_TT, 0x0002);
    qtest_clock_step(s, 2500);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(80-1, 160-1));

    // restart both side
    qtest_writew(s, TAU_TS, 0x0202);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(160-1, 240-1));

    qtest_clock_step(s, 2500);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(80-1, 160-1));

    // stop low side only
    qtest_writew(s, TAU_TT, 0x0002);
    qtest_clock_step(s, 2500);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(160-1, 160-1));
    set_tmr_iflag(s, 1, false);
    set_high_tmr_iflag(s, 1, true);
}

static void test_rl78_tau_interval_timer_8bit_timer_ch1_start_interrupt_loside(void)
{
    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);

    setup_interval_timer(s, 0);

    qtest_writew(s, TAU_TDR(1), tcr_8bit(200-1, 100-1));
    qtest_writew(s, TAU_TPS, 0x0005);
    qtest_writew(s, TAU_TMR(1), 0x0801);

    qtest_writew(s, TAU_TS, 0x0002);
    g_assert_cmpuint(get_tmr_iflag(s, 1), ==, 1);
    g_assert_cmpuint(get_high_tmr_iflag(s, 1), ==, 0);
}

static void test_rl78_tau_interval_timer_8bit_timer_ch1_start_interrupt_hiside(void)
{
    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);

    setup_interval_timer(s, 0);

    qtest_writew(s, TAU_TDR(1), tcr_8bit(200-1, 100-1));
    qtest_writew(s, TAU_TPS, 0x0005);
    qtest_writew(s, TAU_TMR(1), 0x0801);

    qtest_writew(s, TAU_TS, 0x0200);
    g_assert_cmpuint(get_tmr_iflag(s, 1), ==, 0);
    g_assert_cmpuint(get_high_tmr_iflag(s, 1), ==, 1);
}

static void test_rl78_tau_interval_timer_start_interrupt(void)
{
    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);

    setup_interval_timer(s, 0);

    qtest_writew(s, TAU_TDR(1), 3199);
    qtest_writew(s, TAU_TPS, 0x0000);
    qtest_writew(s, TAU_TMR(1), 0x0001);

    qtest_writew(s, TAU_TS, 0x0002);
    g_assert_cmpuint(get_tmr_iflag(s, 1), ==, 1);
    g_assert_cmpuint(get_high_tmr_iflag(s, 1), ==, 0);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rl78/tau/interval_timer/use_ck0", test_rl78_tau_interval_timer_use_ck0);
    qtest_add_func("/rl78/tau/interval_timer/use_ck1", test_rl78_tau_interval_timer_use_ck1);
    qtest_add_func("/rl78/tau/interval_timer/use_ck2_0", test_rl78_tau_interval_timer_use_ck2_0);
    qtest_add_func("/rl78/tau/interval_timer/use_ck2_1", test_rl78_tau_interval_timer_use_ck2_1);
    qtest_add_func("/rl78/tau/interval_timer/use_ck2_2", test_rl78_tau_interval_timer_use_ck2_2);
    qtest_add_func("/rl78/tau/interval_timer/use_ck2_3", test_rl78_tau_interval_timer_use_ck2_3);
    qtest_add_func("/rl78/tau/interval_timer/use_ck3_0", test_rl78_tau_interval_timer_use_ck3_0);
    qtest_add_func("/rl78/tau/interval_timer/use_ck3_1", test_rl78_tau_interval_timer_use_ck3_1);
    qtest_add_func("/rl78/tau/interval_timer/use_ck3_2", test_rl78_tau_interval_timer_use_ck3_2);
    qtest_add_func("/rl78/tau/interval_timer/use_ck3_3", test_rl78_tau_interval_timer_use_ck3_3);

    qtest_add_func("/rl78/tau/interval_timer/using_ck2_0", test_rl78_tau_interval_timer_using_ck2_0);
    qtest_add_func("/rl78/tau/interval_timer/using_ck2_1", test_rl78_tau_interval_timer_using_ck2_1);
    qtest_add_func("/rl78/tau/interval_timer/using_ck2_2", test_rl78_tau_interval_timer_using_ck2_2);
    qtest_add_func("/rl78/tau/interval_timer/using_ck2_3", test_rl78_tau_interval_timer_using_ck2_3);

    qtest_add_func("/rl78/tau/interval_timer/using_ck3_0", test_rl78_tau_interval_timer_using_ck3_0);
    qtest_add_func("/rl78/tau/interval_timer/using_ck3_1", test_rl78_tau_interval_timer_using_ck3_1);
    qtest_add_func("/rl78/tau/interval_timer/using_ck3_2", test_rl78_tau_interval_timer_using_ck3_2);
    qtest_add_func("/rl78/tau/interval_timer/using_ck3_3", test_rl78_tau_interval_timer_using_ck3_3);

    qtest_add_func("/rl78/tau/interval_timer/use_slowest_division", test_rl78_tau_interval_timer_use_slowest_division);
    qtest_add_func("/rl78/tau/interval_timer/stop_timer", test_rl78_tau_interval_timer_stop_timer);

    qtest_add_func("/rl78/tau/interval_timer/8bit_timer_ch1_start_interrupt_loside", test_rl78_tau_interval_timer_8bit_timer_ch1_start_interrupt_loside);
    qtest_add_func("/rl78/tau/interval_timer/8bit_timer_ch1_start_interrupt_hiside", test_rl78_tau_interval_timer_8bit_timer_ch1_start_interrupt_hiside);
    qtest_add_func("/rl78/tau/interval_timer/start_interrupt", test_rl78_tau_interval_timer_start_interrupt);

    qtest_add_func("/rl78/tau/interval_timer/8bit_timer_ch1_lo", test_rl78_tau_interval_timer_8bit_timer_ch1_lo);
    qtest_add_func("/rl78/tau/interval_timer/8bit_timer_ch1_hi", test_rl78_tau_interval_timer_8bit_timer_ch1_hi);
    qtest_add_func("/rl78/tau/interval_timer/8bit_timer_ch3_lo", test_rl78_tau_interval_timer_8bit_timer_ch3_lo);
    qtest_add_func("/rl78/tau/interval_timer/8bit_timer_ch3_hi", test_rl78_tau_interval_timer_8bit_timer_ch3_hi);
    qtest_add_func("/rl78/tau/interval_timer/8bit_timer_simultaneous", test_rl78_tau_interval_timer_8bit_timer_simultaneous);

    ret = g_test_run();

    qtest_end();

    return ret;
}
