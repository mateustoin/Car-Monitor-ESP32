#include <math.h>
#include "unity.h"
#include "shift_advisor.h"

static const float R[5] = {7.5f, 13.6f, 20.0f, 27.1f, 36.9f};

static shift_advice_t adv(uint8_t gear, float rpm, float load, float thr, uint8_t *tgt)
{
    const vehicle_profile_t *p = vp_default();
    gear_input_t in = {.now_ms = 0, .sample_ms = 0, .rpm = rpm, .speed_kmh = 50,
                       .load_pct = load, .throttle_pct = thr};
    return shift_advise(p, R, 5, gear, &in, tgt);
}

static void test_upshift_light_load(void)
{
    uint8_t t = 9;
    TEST_ASSERT_EQUAL(SHIFT_UP, adv(3, 2200, 15, 20, &t));
    TEST_ASSERT_EQUAL_UINT8(4, t);
}

static void test_no_upshift_below_limit_heavy(void)
{
    uint8_t t = 9;
    TEST_ASSERT_EQUAL(SHIFT_NONE, adv(3, 2500, 85, 60, &t));
    TEST_ASSERT_EQUAL_UINT8(0, t);
}

static void test_no_upshift_if_lugs_next(void)
{
    uint8_t t = 9;
    TEST_ASSERT_EQUAL(SHIFT_NONE, adv(1, 2200, 15, 20, &t));
    TEST_ASSERT_EQUAL_UINT8(0, t);
}

static void test_downshift_skips(void)
{
    uint8_t t = 0;
    TEST_ASSERT_EQUAL(SHIFT_DOWN, adv(5, 1200, 80, 70, &t));
    TEST_ASSERT_EQUAL_UINT8(3, t);
}

static void test_no_downshift_throttle_closed(void)
{
    uint8_t t = 9;
    TEST_ASSERT_EQUAL(SHIFT_NONE, adv(5, 1200, 80, 1, &t));
    TEST_ASSERT_EQUAL_UINT8(0, t);
}

static void test_no_downshift_closed_by_load_fallback(void)
{
    uint8_t t = 9;
    TEST_ASSERT_EQUAL(SHIFT_NONE, adv(5, 1200, 10, NAN, &t));
    TEST_ASSERT_EQUAL_UINT8(0, t);
}

static void test_downshift_without_throttle_sensor(void)
{
    uint8_t t = 0;
    TEST_ASSERT_EQUAL(SHIFT_DOWN, adv(5, 1200, 80, NAN, &t));
    TEST_ASSERT_EQUAL_UINT8(3, t);
}

static void test_top_gear_no_up(void)
{
    uint8_t t = 9;
    TEST_ASSERT_EQUAL(SHIFT_NONE, adv(5, 3000, 15, 20, &t));
    TEST_ASSERT_EQUAL_UINT8(0, t);
}

static void test_no_advice_without_gear(void)
{
    uint8_t t = 9;
    TEST_ASSERT_EQUAL(SHIFT_NONE, adv(0, 3000, 15, 20, &t));
    TEST_ASSERT_EQUAL_UINT8(0, t);
}

static void test_downshift_falls_back_to_previous_gear(void)
{
    /* nenhuma marcha prevê RPM na faixa 2000..redline -> alvo = gear - 1 */
    static const float close[3] = {10.0f, 11.0f, 12.0f};
    gear_input_t in = {.rpm = 1000, .speed_kmh = 12, .load_pct = 80, .throttle_pct = 70};
    uint8_t t = 0;
    TEST_ASSERT_EQUAL(SHIFT_DOWN, shift_advise(vp_default(), close, 3, 3, &in, &t));
    TEST_ASSERT_EQUAL_UINT8(2, t);
}

static void test_no_downshift_above_redline(void)
{
    /* 1200 RPM na 2ª com 1ª muito curta: previsto 9600 > redline (6000) -> sem conselho */
    static const float steep[2] = {10.0f, 80.0f};
    gear_input_t in = {.rpm = 1200, .speed_kmh = 96, .load_pct = 80, .throttle_pct = 70};
    uint8_t t = 9;
    TEST_ASSERT_EQUAL(SHIFT_NONE, shift_advise(vp_default(), steep, 2, 2, &in, &t));
    TEST_ASSERT_EQUAL_UINT8(0, t);
}

void run_shift_advisor_tests(void)
{
    RUN_TEST(test_upshift_light_load);
    RUN_TEST(test_no_upshift_below_limit_heavy);
    RUN_TEST(test_no_upshift_if_lugs_next);
    RUN_TEST(test_downshift_skips);
    RUN_TEST(test_no_downshift_throttle_closed);
    RUN_TEST(test_no_downshift_closed_by_load_fallback);
    RUN_TEST(test_downshift_without_throttle_sensor);
    RUN_TEST(test_no_downshift_above_redline);
    RUN_TEST(test_top_gear_no_up);
    RUN_TEST(test_no_advice_without_gear);
    RUN_TEST(test_downshift_falls_back_to_previous_gear);
}
