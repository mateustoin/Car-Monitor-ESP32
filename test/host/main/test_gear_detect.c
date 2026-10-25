#include <stdint.h>
#include "unity.h"
#include "gear_detect.h"

static const float R[5] = {7.5f, 13.6f, 20.0f, 27.1f, 36.9f};
#define TOL  0.07f
#define HOLD 400

static uint8_t upd(gear_detect_t *d, uint8_t n, float rpm, float spd, uint32_t t)
{
    return gear_detect_update(d, R, n, rpm, spd, TOL, HOLD, t);
}

/* alimenta amostras a cada 100 ms em [from, to] e devolve a última resposta */
static uint8_t run(gear_detect_t *d, float rpm, float spd, uint32_t from, uint32_t to)
{
    uint8_t g = 0;
    for (uint32_t t = from; t <= to; t += 100) {
        g = upd(d, 5, rpm, spd, t);
    }
    return g;
}

static void test_detects_third(void)
{
    gear_detect_t d;
    gear_detect_init(&d);
    for (uint32_t t = 0; t <= 300; t += 100) {
        TEST_ASSERT_EQUAL_UINT8(0, upd(&d, 5, 2000, 40, t));
    }
    for (uint32_t t = 400; t <= 800; t += 100) {
        TEST_ASSERT_EQUAL_UINT8(3, upd(&d, 5, 2000, 40, t));
    }
}

static void test_zero_rpm_or_speed(void)
{
    gear_detect_t d;
    gear_detect_init(&d);
    TEST_ASSERT_EQUAL_UINT8(0, upd(&d, 5, 0, 50, 0));
    TEST_ASSERT_EQUAL_UINT8(0, upd(&d, 5, 900, 0, 100));
    /* de uma marcha exibida, a condição persistindo hold_ms leva a 0 */
    gear_detect_init(&d);
    TEST_ASSERT_EQUAL_UINT8(3, run(&d, 2000, 40, 0, 500));
    TEST_ASSERT_EQUAL_UINT8(0, run(&d, 0, 50, 600, 1100));
    gear_detect_init(&d);
    TEST_ASSERT_EQUAL_UINT8(3, run(&d, 2000, 40, 0, 500));
    TEST_ASSERT_EQUAL_UINT8(0, run(&d, 900, 0, 600, 1100));
}

static void test_clutch_in_shows_dash(void)
{
    gear_detect_t d;
    gear_detect_init(&d);
    TEST_ASSERT_EQUAL_UINT8(3, run(&d, 2000, 40, 0, 1000));
    /* 1500 RPM a 60 km/h: relação 40, sem casar */
    TEST_ASSERT_EQUAL_UINT8(0, run(&d, 1500, 60, 1100, 1600));
}

static void test_no_flicker_on_shift(void)
{
    gear_detect_t d;
    gear_detect_init(&d);
    TEST_ASSERT_EQUAL_UINT8(3, run(&d, 2000, 40, 0, 1000));
    for (uint32_t t = 1100; t <= 1200; t += 100) {
        TEST_ASSERT_EQUAL_UINT8(3, upd(&d, 5, 1500, 60, t));
    }
    /* 4ª: 2000 RPM a 54.2 km/h (relação 27.1) */
    for (uint32_t t = 1300; t <= 1600; t += 100) {
        TEST_ASSERT_EQUAL_UINT8(3, upd(&d, 5, 2000, 54.2f, t));
    }
    TEST_ASSERT_EQUAL_UINT8(4, upd(&d, 5, 2000, 54.2f, 1700));
}

static void test_partial_ratios(void)
{
    gear_detect_t d;
    gear_detect_init(&d);
    /* casa com a 4ª real, mas só 2 marchas conhecidas */
    uint8_t g = 0;
    for (uint32_t t = 0; t <= 1000; t += 100) {
        g = upd(&d, 2, 2000, 54.2f, t);
    }
    TEST_ASSERT_EQUAL_UINT8(0, g);
}

void run_gear_detect_tests(void)
{
    RUN_TEST(test_detects_third);
    RUN_TEST(test_zero_rpm_or_speed);
    RUN_TEST(test_clutch_in_shows_dash);
    RUN_TEST(test_no_flicker_on_shift);
    RUN_TEST(test_partial_ratios);
}
