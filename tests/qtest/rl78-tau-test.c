#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqtest-single.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"

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

static void setup_interval_timer(QTestState *s, const uint8_t channel)
{
    qtest_writew(s, TAU_TOE, 0x0000);
    qtest_writew(s, TAU_TO, 0x0000);
    qtest_writew(s, TAU_TOL, 0x0000);
    qtest_writeb(s, TAU_TIS0, 0x00);
    qtest_writeb(s, TAU_TIS1, 0x00);
}

static void test_rl78_tau_interval_timer_basic_usage(void)
{
    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);
    qtest_irq_intercept_out_named(s, "/machine/mcu/tau[0]", SYSBUS_DEVICE_GPIO_IRQ);

    setup_interval_timer(s, 0);

    // 100us interval
    qtest_writew(s, TAU_TDR(0), 3199);
    qtest_writew(s, TAU_TPS, 0x0010);

    qtest_writew(s, TAU_TMR(0), 0x0000);
    qtest_writew(s, TAU_TS, 0x0001);
    
    qtest_clock_step(s, 1);
 
    // step 50us
    qtest_clock_step(s, 50*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(0)), ==, 1599);
    g_assert_cmpuint(qtest_get_irq(s, 0), ==, 0);

    // step 50us(interval timer should be up)
    qtest_clock_step(s, 50*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(0)), ==, 3199);
    g_assert_cmpuint(qtest_get_irq(s, 0), ==, 1);

    // step 50us
    qtest_clock_step(s, 50*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(0)), ==, 1599);
}

static void test_rl78_tau_interval_timer_using_divider(void)
{
    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);
    qtest_irq_intercept_out_named(s, "/machine/mcu/tau[0]", SYSBUS_DEVICE_GPIO_IRQ);

    setup_interval_timer(s, 0);

    // 200us interval
    qtest_writew(s, TAU_TDR(0), 3199);
    qtest_writew(s, TAU_TPS, 0x0010);

    qtest_writew(s, TAU_TMR(0), 0x8000);
    qtest_writew(s, TAU_TS, 0x0001);
    
    qtest_clock_step(s, 1);
 
    // step 100us
    qtest_clock_step(s, 100*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(0)), ==, 1599);
    g_assert_cmpuint(qtest_get_irq(s, 0), ==, 0);

    // step 100us(interval timer should be up)
    qtest_clock_step(s, 100*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(0)), ==, 3199);
    g_assert_cmpuint(qtest_get_irq(s, 0), ==, 1);

    // step 100us
    qtest_clock_step(s, 100*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(0)), ==, 1599);
}

static void rl78_tau_interval_timer_using_ck2(const uint16_t tps, const uint32_t interval)
{
    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);
    qtest_irq_intercept_out_named(s, "/machine/mcu/tau[0]", SYSBUS_DEVICE_GPIO_IRQ);

    setup_interval_timer(s, 0);

    qtest_writew(s, TAU_TDR(1), 3199);
    qtest_writew(s, TAU_TPS, 0x0010 | (tps << 8));

    qtest_writew(s, TAU_TMR(1), 0x4000);
    qtest_writew(s, TAU_TS, 0x0002);
    
    qtest_clock_step(s, 1);
 
    qtest_clock_step(s, interval / 2);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, 1599);
    g_assert_cmpuint(qtest_get_irq(s, 1), ==, 0);

    qtest_clock_step(s, interval / 2);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, 3199);
    g_assert_cmpuint(qtest_get_irq(s, 1), ==, 1);
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
    qtest_irq_intercept_out_named(s, "/machine/mcu/tau[0]", SYSBUS_DEVICE_GPIO_IRQ);

    setup_interval_timer(s, 0);

    qtest_writew(s, TAU_TDR(1), 3199);
    qtest_writew(s, TAU_TPS, 0x0010 | (tps << 12));

    qtest_writew(s, TAU_TMR(1), 0xC000);
    qtest_writew(s, TAU_TS, 0x0002);
    
    qtest_clock_step(s, 1);
 
    qtest_clock_step(s, interval / 2);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, 1599);
    g_assert_cmpuint(qtest_get_irq(s, 1), ==, 0);

    qtest_clock_step(s, interval / 2);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, 3199);
    g_assert_cmpuint(qtest_get_irq(s, 1), ==, 1);
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
    qtest_irq_intercept_out_named(s, "/machine/mcu/tau[0]", SYSBUS_DEVICE_GPIO_IRQ);

    setup_interval_timer(s, 0);

    // 100us interval
    qtest_writew(s, TAU_TDR(0), 3199);
    qtest_writew(s, TAU_TPS, 0x0010);

    qtest_writew(s, TAU_TMR(0), 0x0000);
    qtest_writew(s, TAU_TS, 0x0001);
    
    qtest_clock_step(s, 1);
 
    // step 50us
    qtest_clock_step(s, 50*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(0)), ==, 1599);

    // stop timer
    qtest_writew(s, TAU_TT, 0x0001);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(0)), ==, 1599);
    
    // step 50us
    qtest_clock_step(s, 50*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(0)), ==, 1599);
    g_assert_cmpuint(qtest_get_irq(s, 0), ==, 0);

    // restart timer
    qtest_writew(s, TAU_TS, 0x0001);
    qtest_clock_step(s, 1);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(0)), ==, 3199);
    g_assert_cmpuint(qtest_get_irq(s, 0), ==, 0);

    qtest_clock_step(s, 50*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(0)), ==, 1599);
    g_assert_cmpuint(qtest_get_irq(s, 0), ==, 0);
}

static uint16_t tcr_8bit(const uint8_t hi, const uint8_t lo) 
{
    return ((uint16_t)hi << 8) | (uint16_t)lo;
}

static void test_rl78_tau_interval_timer_8bit_timer_ch1_loside(void)
{
    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);
    qtest_irq_intercept_out_named(s, "/machine/mcu/tau[0]", SYSBUS_DEVICE_GPIO_IRQ);

    setup_interval_timer(s, 0);

    qtest_writew(s, TAU_TDR(1), tcr_8bit(200-1, 100-1));
    qtest_writew(s, TAU_TPS, 0x0005);
    qtest_writew(s, TAU_TMR(1), 0x0800);
    qtest_writew(s, TAU_TS, 0x0002);

    qtest_clock_step(s, 1);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(0xFF, 100-1)); 

    qtest_clock_step(s, 50*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(0xFF, 50-1)); 
    g_assert_cmpuint(qtest_get_irq(s, 1), ==, 0);
    g_assert_cmpuint(qtest_get_irq(s, 9), ==, 0);

    qtest_clock_step(s, 50*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(0xFF, 100-1)); 
    g_assert_cmpuint(qtest_get_irq(s, 1), ==, 1);
    g_assert_cmpuint(qtest_get_irq(s, 9), ==, 0);
}

static void test_rl78_tau_interval_timer_8bit_timer_ch1_hiside(void)
{
    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);
    qtest_irq_intercept_out_named(s, "/machine/mcu/tau[0]", SYSBUS_DEVICE_GPIO_IRQ);

    setup_interval_timer(s, 0);

    qtest_writew(s, TAU_TDR(1), tcr_8bit(200-1, 100-1));
    qtest_writew(s, TAU_TPS, 0x0005);
    qtest_writew(s, TAU_TMR(1), 0x0800);
    qtest_writew(s, TAU_TS, 0x0200);

    qtest_clock_step(s, 1);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(200-1, 0xFF)); 

    qtest_clock_step(s, 100*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(100-1, 0xFF)); 
    g_assert_cmpuint(qtest_get_irq(s, 1), ==, 0);
    g_assert_cmpuint(qtest_get_irq(s, 9), ==, 0);

    qtest_clock_step(s, 100*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(200-1, 0xFF)); 
    g_assert_cmpuint(qtest_get_irq(s, 1), ==, 0);
    g_assert_cmpuint(qtest_get_irq(s, 9), ==, 1);
}

static void test_rl78_tau_interval_timer_8bit_timer_ch1_bothside(void)
{
    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);
    qtest_irq_intercept_out_named(s, "/machine/mcu/tau[0]", SYSBUS_DEVICE_GPIO_IRQ);

    setup_interval_timer(s, 0);

    qtest_writew(s, TAU_TDR(1), tcr_8bit(200-1, 100-1));
    qtest_writew(s, TAU_TPS, 0x0005);
    qtest_writew(s, TAU_TMR(1), 0x0800);

    qtest_writew(s, TAU_TS, 0x0200);
    qtest_clock_step(s, 1);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(200-1, 0xFF)); 

    qtest_clock_step(s, 50*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(150-1, 0xFF)); 
    g_assert_cmpuint(qtest_get_irq(s, 1), ==, 0);
    g_assert_cmpuint(qtest_get_irq(s, 9), ==, 0);

    qtest_writew(s, TAU_TS, 0x0002);
    qtest_clock_step(s, 1);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(150-1, 100-1)); 
    g_assert_cmpuint(qtest_get_irq(s, 1), ==, 0);
    g_assert_cmpuint(qtest_get_irq(s, 9), ==, 0);

    qtest_clock_step(s, 50*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(100-1, 50-1)); 
    g_assert_cmpuint(qtest_get_irq(s, 1), ==, 0);
    g_assert_cmpuint(qtest_get_irq(s, 9), ==, 0);

    qtest_clock_step(s, 50*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(50-1, 100-1)); 
    g_assert_cmpuint(qtest_get_irq(s, 1), ==, 1);
    g_assert_cmpuint(qtest_get_irq(s, 9), ==, 0);

    qtest_clock_step(s, 50*1000);
    g_assert_cmpuint(qtest_readw(s, TAU_TCR(1)), ==, tcr_8bit(200-1, 50-1)); 
    g_assert_cmpuint(qtest_get_irq(s, 1), ==, 1);
    g_assert_cmpuint(qtest_get_irq(s, 9), ==, 1);
}

static void test_rl78_tau_interval_timer_8bit_timer_ch1_start_interrupt_loside(void)
{
    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);
    qtest_irq_intercept_out_named(s, "/machine/mcu/tau[0]", SYSBUS_DEVICE_GPIO_IRQ);

    setup_interval_timer(s, 0);

    qtest_writew(s, TAU_TDR(1), tcr_8bit(200-1, 100-1));
    qtest_writew(s, TAU_TPS, 0x0005);
    qtest_writew(s, TAU_TMR(1), 0x0801);

    qtest_writew(s, TAU_TS, 0x0002);
    g_assert_cmpuint(qtest_get_irq(s, 1), ==, 1);
    g_assert_cmpuint(qtest_get_irq(s, 9), ==, 0);
}

static void test_rl78_tau_interval_timer_8bit_timer_ch1_start_interrupt_hiside(void)
{
    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);
    qtest_irq_intercept_out_named(s, "/machine/mcu/tau[0]", SYSBUS_DEVICE_GPIO_IRQ);

    setup_interval_timer(s, 0);

    qtest_writew(s, TAU_TDR(1), tcr_8bit(200-1, 100-1));
    qtest_writew(s, TAU_TPS, 0x0005);
    qtest_writew(s, TAU_TMR(1), 0x0801);

    qtest_writew(s, TAU_TS, 0x0200);
    g_assert_cmpuint(qtest_get_irq(s, 1), ==, 0);
    g_assert_cmpuint(qtest_get_irq(s, 9), ==, 1);
}

static void test_rl78_tau_interval_timer_start_interrupt(void)
{
    QTestState *s = qtest_init("-M virt -nographic");

    qtest_system_reset(s);
    qtest_irq_intercept_out_named(s, "/machine/mcu/tau[0]", SYSBUS_DEVICE_GPIO_IRQ);

    setup_interval_timer(s, 0);

    qtest_writew(s, TAU_TDR(1), 3199);
    qtest_writew(s, TAU_TPS, 0x0000);
    qtest_writew(s, TAU_TMR(1), 0x0001);

    qtest_writew(s, TAU_TS, 0x0002);
    g_assert_cmpuint(qtest_get_irq(s, 1), ==, 1);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rl78/tau/interval_timer/basic_usage", test_rl78_tau_interval_timer_basic_usage);
    qtest_add_func("/rl78/tau/interval_timer/using_divider", test_rl78_tau_interval_timer_using_divider);
    qtest_add_func("/rl78/tau/interval_timer/using_ck2_0", test_rl78_tau_interval_timer_using_ck2_0);
    qtest_add_func("/rl78/tau/interval_timer/using_ck2_1", test_rl78_tau_interval_timer_using_ck2_1);
    qtest_add_func("/rl78/tau/interval_timer/using_ck2_2", test_rl78_tau_interval_timer_using_ck2_2);
    qtest_add_func("/rl78/tau/interval_timer/using_ck2_3", test_rl78_tau_interval_timer_using_ck2_3);

    qtest_add_func("/rl78/tau/interval_timer/using_ck3_0", test_rl78_tau_interval_timer_using_ck3_0);
    qtest_add_func("/rl78/tau/interval_timer/using_ck3_1", test_rl78_tau_interval_timer_using_ck3_1);
    qtest_add_func("/rl78/tau/interval_timer/using_ck3_2", test_rl78_tau_interval_timer_using_ck3_2);
    qtest_add_func("/rl78/tau/interval_timer/using_ck3_3", test_rl78_tau_interval_timer_using_ck3_3);

    qtest_add_func("/rl78/tau/interval_timer/stop_timer", test_rl78_tau_interval_timer_stop_timer);

    qtest_add_func("/rl78/tau/interval_timer/8bit_timer_ch1_loside", test_rl78_tau_interval_timer_8bit_timer_ch1_loside);
    qtest_add_func("/rl78/tau/interval_timer/8bit_timer_ch1_hiside", test_rl78_tau_interval_timer_8bit_timer_ch1_hiside);
    qtest_add_func("/rl78/tau/interval_timer/8bit_timer_ch1_bothside", test_rl78_tau_interval_timer_8bit_timer_ch1_bothside);

    qtest_add_func("/rl78/tau/interval_timer/8bit_timer_ch1_start_interrupt_loside", test_rl78_tau_interval_timer_8bit_timer_ch1_start_interrupt_loside);
    qtest_add_func("/rl78/tau/interval_timer/8bit_timer_ch1_start_interrupt_hiside", test_rl78_tau_interval_timer_8bit_timer_ch1_start_interrupt_hiside);
    qtest_add_func("/rl78/tau/interval_timer/start_interrupt", test_rl78_tau_interval_timer_start_interrupt);

    ret = g_test_run();

    qtest_end();

    return ret;
}
