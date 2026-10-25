#include <stdint.h>
#include <math.h>
#include "unity.h"
#include "gear_learn.h"

static uint32_t s_rng;

static float rnd01(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return (float)(s_rng >> 8) / (float)(1u << 24);
}

/* ruído uniforme de ±2% */
static void feed(gear_learn_t *l, float ratio, int n)
{
    for (int i = 0; i < n; i++) {
        gear_learn_add(l, ratio * (1.0f + (rnd01() * 2.0f - 1.0f) * 0.02f));
    }
}

static void check_close(const float *want, const float *got, int n)
{
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_FLOAT_WITHIN(want[i] * 0.03f, want[i], got[i]);
    }
}

static const float FIVE[5] = {7.5f, 13.6f, 20.0f, 27.1f, 36.9f};

static void test_learns_five(void)
{
    s_rng = 12345;
    gear_learn_t l;
    float out[VP_MAX_GEARS];
    gear_learn_init(&l, 5);
    for (int g = 0; g < 5; g++) feed(&l, FIVE[g], 300);
    TEST_ASSERT_EQUAL_UINT8(5, gear_learn_result(&l, out));
    check_close(FIVE, out, 5);
    TEST_ASSERT_TRUE(gear_learn_converged(&l));
}

static void test_learns_six_gear_profile(void)
{
    s_rng = 777;
    static const float six[6] = {7.0f, 12.0f, 17.5f, 23.0f, 29.0f, 35.0f};
    gear_learn_t l;
    float out[VP_MAX_GEARS];
    gear_learn_init(&l, 6);
    for (int g = 0; g < 6; g++) feed(&l, six[g], 300);
    TEST_ASSERT_EQUAL_UINT8(6, gear_learn_result(&l, out));
    check_close(six, out, 6);
    TEST_ASSERT_TRUE(gear_learn_converged(&l));
}

static void test_transition_noise_rejected(void)
{
    s_rng = 999;
    gear_learn_t l;
    float out[VP_MAX_GEARS];
    gear_learn_init(&l, 5);
    for (int g = 0; g < 5; g++) feed(&l, FIVE[g], 300);
    for (int i = 0; i < 150; i++) gear_learn_add(&l, 5.0f + rnd01() * 35.0f);
    TEST_ASSERT_EQUAL_UINT8(5, gear_learn_result(&l, out));
    check_close(FIVE, out, 5);
}

static void test_partial_never_fifth(void)
{
    s_rng = 4242;
    gear_learn_t l;
    float out[VP_MAX_GEARS];
    gear_learn_init(&l, 5);
    for (int g = 0; g < 4; g++) feed(&l, FIVE[g], 300);
    TEST_ASSERT_EQUAL_UINT8(4, gear_learn_result(&l, out));
    check_close(FIVE, out, 4);
    TEST_ASSERT_FALSE(gear_learn_converged(&l));
}

static void test_too_few_samples(void)
{
    s_rng = 1;
    gear_learn_t l;
    float out[VP_MAX_GEARS];
    gear_learn_init(&l, 5);
    feed(&l, 20.0f, 20);
    TEST_ASSERT_EQUAL_UINT8(0, gear_learn_result(&l, out));
    TEST_ASSERT_FALSE(gear_learn_converged(&l));
}

static void test_invalid_samples_ignored(void)
{
    gear_learn_t l;
    float out[VP_MAX_GEARS];
    gear_learn_init(&l, 5);
    for (int i = 0; i < 100; i++) {
        gear_learn_add(&l, 0.0f);
        gear_learn_add(&l, -3.0f);
        gear_learn_add(&l, NAN);
        gear_learn_add(&l, 500.0f);
    }
    TEST_ASSERT_EQUAL_UINT8(0, gear_learn_result(&l, out));
}

static void test_more_clusters_than_gears_keeps_most_populated(void)
{
    s_rng = 55;
    gear_learn_t l;
    float out[VP_MAX_GEARS];
    gear_learn_init(&l, 2);
    feed(&l, 10.0f, 100);
    feed(&l, 20.0f, 300);
    feed(&l, 30.0f, 200);
    TEST_ASSERT_EQUAL_UINT8(2, gear_learn_result(&l, out));
    TEST_ASSERT_FLOAT_WITHIN(0.6f, 20.0f, out[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.9f, 30.0f, out[1]);
}

void run_gear_learn_tests(void)
{
    RUN_TEST(test_learns_five);
    RUN_TEST(test_learns_six_gear_profile);
    RUN_TEST(test_transition_noise_rejected);
    RUN_TEST(test_partial_never_fifth);
    RUN_TEST(test_too_few_samples);
    RUN_TEST(test_invalid_samples_ignored);
    RUN_TEST(test_more_clusters_than_gears_keeps_most_populated);
}
