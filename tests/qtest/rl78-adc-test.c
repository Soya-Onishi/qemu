#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqtest-single.h"
#include "qemu/module.h"

#define ADC_ADM0 0xFFF30
#define ADC_ADM1 0xFFF32
#define ADC_ADM2 0xF0010
#define ADC_ADCRn(n) (0xF0020 + ((n) * 2))
#define ADC_ADCR 0xFFF1E
#define ADC_ADCRnH(n) (0xF0021 + ((n) * 2))
#define ADC_ADCRH 0xFFF1F
#define ADC_ADS 0xFFF31
#define ADC_ADUL 0xF0011
#define ADC_ADLL 0xF0012
#define ADC_ADTES 0xF0013

#define ADC_ADM0_ADCS 0x80

static void test_rl78_adc_select_oneshot_mode(void)
{
    QTestState *s = qtest_init("-M qtest -nographic");

    qtest_qmp(s, 
        "{ 'execute': 'qom-set', "
        "  'arguments': { "
        "    'path': '/machine/mcu/adc', " 
        "    'property': 'adc-result[0]', "
        "    'value': 2.5"
        "}}"
    );

    qtest_qmp(s, 
        "{ 'execute': 'qom-set', "
        "  'arguments': { "
        "    'path': '/machine/mcu/adc', " 
        "    'property': 'adc-result[1]', "
        "    'value': 5.0"
        "}}"
    );

    qtest_qmp(s, 
        "{ 'execute': 'qom-set', "
        "  'arguments': { "
        "    'path': '/machine/mcu/adc', " 
        "    'property': 'adc-result[2]', "
        "    'value': 0.0"
        "}}"
    );


    qtest_writeb(s, ADC_ADM0, 0x00);
    qtest_writeb(s, ADC_ADM1, 0x20);
    qtest_writeb(s, ADC_ADM2, 0x00);
    qtest_writeb(s, ADC_ADS, 0x00);
    qtest_writeb(s, ADC_ADM0, 0x80);

    qtest_clock_step(s, 1);
    g_assert_cmpuint(qtest_readb(s, ADC_ADM0), ==, 0x80);

    qtest_clock_step_next(s);
    g_assert_cmpuint(qtest_readb(s, ADC_ADM0), ==, 0x00);
    g_assert_cmpuint(qtest_readw(s, ADC_ADCR) >> 6, ==, 0x0200);
}

static void test_rl78_adc_scan_oneshot_mode(void)
{
    QTestState *s = qtest_init("-M qtest -nographic");

    const uint8_t adm0 = 0x40;
    qtest_writeb(s, ADC_ADM0, adm0);
    qtest_writeb(s, ADC_ADM1, 0x20);
    qtest_writeb(s, ADC_ADM2, 0x00);
    qtest_writeb(s, ADC_ADS, 0x00);
    qtest_writeb(s, ADC_ADM0, adm0 | ADC_ADM0_ADCS);

    qtest_clock_step(s, 1);
    g_assert_cmpuint(qtest_readb(s, ADC_ADM0), ==, adm0 | ADC_ADM0_ADCS);

    qtest_clock_step_next(s);
    g_assert_cmpuint(qtest_readb(s, ADC_ADM0), ==, adm0 | ADC_ADM0_ADCS);
    qtest_clock_step_next(s);
    g_assert_cmpuint(qtest_readb(s, ADC_ADM0), ==, adm0 | ADC_ADM0_ADCS);
    qtest_clock_step_next(s);
    g_assert_cmpuint(qtest_readb(s, ADC_ADM0), ==, adm0 | ADC_ADM0_ADCS);
    qtest_clock_step_next(s);
    g_assert_cmpuint(qtest_readb(s, ADC_ADM0), ==, adm0);
}

static void test_rl78_adc_select_continuous_mode(void)
{
    QTestState *s = qtest_init("-M virt -nographic");

    const uint8_t adm0 = 0x00;
    qtest_writeb(s, ADC_ADM0, adm0);
    qtest_writeb(s, ADC_ADM1, 0x00);
    qtest_writeb(s, ADC_ADM2, 0x00);
    qtest_writeb(s, ADC_ADS, 0x00);
    qtest_writeb(s, ADC_ADM0, adm0 | ADC_ADM0_ADCS);

    qtest_clock_step(s, 1);
    g_assert_cmpuint(qtest_readb(s, ADC_ADM0), ==, adm0 | ADC_ADM0_ADCS);

    qtest_clock_step_next(s);
    g_assert_cmpuint(qtest_readb(s, ADC_ADM0), ==, adm0 | ADC_ADM0_ADCS);
    qtest_clock_step_next(s);
    g_assert_cmpuint(qtest_readb(s, ADC_ADM0), ==, adm0 | ADC_ADM0_ADCS);
}

static void test_rl78_adc_scan_continuous_mode(void)
{
    QTestState *s = qtest_init("-M virt -nographic");

    const uint8_t adm0 = 0x00;
    qtest_writeb(s, ADC_ADM0, adm0);
    qtest_writeb(s, ADC_ADM1, 0x00);
    qtest_writeb(s, ADC_ADM2, 0x00);
    qtest_writeb(s, ADC_ADS, 0x00);
    qtest_writeb(s, ADC_ADM0, adm0 | ADC_ADM0_ADCS);

    qtest_clock_step(s, 1);
    qtest_clock_step_next(s);
    qtest_clock_step_next(s);
    qtest_clock_step_next(s);
    qtest_clock_step_next(s);
    g_assert_cmpuint(qtest_readb(s, ADC_ADM0), ==, adm0 | ADC_ADM0_ADCS);

    qtest_clock_step_next(s);
    qtest_clock_step_next(s);
    qtest_clock_step_next(s);
    qtest_clock_step_next(s);
    g_assert_cmpuint(qtest_readb(s, ADC_ADM0), ==, adm0 | ADC_ADM0_ADCS);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rl78/adc/select_oneshot_mode", test_rl78_adc_select_oneshot_mode);
    qtest_add_func("/rl78/adc/scan_oneshot_mode", test_rl78_adc_scan_oneshot_mode);
    qtest_add_func("/rl78/adc/select_continuous_mode", test_rl78_adc_select_continuous_mode);
    qtest_add_func("/rl78/adc/scan_continuous_mode", test_rl78_adc_scan_continuous_mode);

    ret = g_test_run();

    qtest_end();

    return ret;
}
