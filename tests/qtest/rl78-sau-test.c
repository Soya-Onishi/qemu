#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqtest-single.h"
#include "qemu/module.h"

#define SAU_SPS 0xF0126
#define SAU_SMR 0xF0110
#define SAU_SCR 0xF0118
#define SAU_SDR 0xFFF10
#define SAU_SOL 0xF0134
#define SAU_SO  0xF0128
#define SAU_SOE 0xF012A
#define SAU_SS  0xF0122
#define SAU_SSR 0xF0100

static void test_rl78_sau_send_byte(void)
{
    QTestState *s = qtest_init("-M virt -nographic -d unimp");

    // initial settings
    qtest_system_reset(s);
    qtest_writew(s, SAU_SPS, 0x0088);
    qtest_writew(s, SAU_SMR, 0x0022);
    qtest_writew(s, SAU_SCR, 0x8097);
    qtest_writew(s, SAU_SDR, 51 << 9);
    qtest_writew(s, SAU_SOL, 0x0000);
    qtest_writew(s, SAU_SO, 0x0101);
    qtest_writew(s, SAU_SOE, 0x0001);
    qtest_writew(s, SAU_SS, 0x0001);

    // send byte
    qtest_writew(s, SAU_SDR, 0x0000 | 'a');

    uint16_t ssr = qtest_readw(s, SAU_SSR);
    g_assert_cmpuint(ssr, ==, 0x0040);

    qtest_clock_step_next(s);
    ssr = qtest_readw(s, SAU_SSR);
    g_assert_cmpuint(ssr, ==, 0x0000);
}

int main(int argc, char **argv)
{
    int ret;
    
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rl78/sau/send_byte", test_rl78_sau_send_byte);

    ret = g_test_run();

    qtest_end();

    return ret;
}
