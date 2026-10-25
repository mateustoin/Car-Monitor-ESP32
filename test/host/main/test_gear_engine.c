#include <math.h>
#include "unity.h"
#include "gear_engine.h"
#include "test_profiles.h"

static const float R5[5] = {7.5f, 13.6f, 20.0f, 27.1f, 36.9f};

static gear_input_t mk(uint32_t t, float rpm, float spd, float load)
{
    gear_input_t in = {.now_ms = t, .sample_ms = t, .rpm = rpm, .speed_kmh = spd,
                       .load_pct = load, .throttle_pct = 30};
    return in;
}

/* alimenta a 10 Hz por dur_ms; devolve o último estado e avança *t */
static gear_state_t feed(gear_engine_t *g, uint32_t *t, float rpm, float spd, float load, uint32_t dur_ms)
{
    gear_state_t s = {0};
    for (uint32_t e = 0; e < dur_ms; e += 100) {
        gear_input_t in = mk(*t, rpm, spd, load);
        s = gear_engine_update(g, &in);
        *t += 100;
    }
    return s;
}

static void test_stale_clears(void)
{
    gear_engine_t g;
    uint32_t t = 0;
    gear_engine_init(&g, vp_default(), R5); /* Sandero sem sementes: relações vindas do NVS */
    /* 3ª: 2200 RPM a 44 km/h (20.0), carga 15 -> UP */
    gear_state_t s = feed(&g, &t, 2200, 44, 15, 2000);
    TEST_ASSERT_EQUAL_UINT8(3, s.gear);
    TEST_ASSERT_EQUAL(SHIFT_UP, s.advice);
    TEST_ASSERT_EQUAL_UINT8(4, s.target_gear);
    TEST_ASSERT_FALSE(s.stale);
    gear_input_t in = mk(t, 2200, 44, 15);
    in.sample_ms = t - 1500;
    s = gear_engine_update(&g, &in);
    TEST_ASSERT_EQUAL_UINT8(0, s.gear);
    TEST_ASSERT_EQUAL(SHIFT_NONE, s.advice);
    TEST_ASSERT_EQUAL_UINT8(0, s.target_gear);
    TEST_ASSERT_TRUE(s.stale);
}

static void test_first_boot_no_seeds(void)
{
    gear_engine_t g;
    uint32_t t = 0;
    gear_engine_init(&g, vp_find("generic_man"), NULL);
    gear_state_t s = feed(&g, &t, 2200, 44, 30, 2000);
    TEST_ASSERT_TRUE(s.learning);
    TEST_ASSERT_EQUAL_UINT8(0, s.learned_gears);
    TEST_ASSERT_EQUAL_UINT8(0, s.gear);
    TEST_ASSERT_EQUAL(SHIFT_NONE, s.advice);
}

static void test_learns_then_reports_once(void)
{
    gear_engine_t g;
    uint32_t t = 0;
    float out[VP_MAX_GEARS] = {0};
    gear_engine_init(&g, vp_find("generic_man"), NULL);
    for (int i = 0; i < 5; i++) {
        /* rpm 2000 -> velocidade = ratio*2 (1ª = 15 km/h) */
        feed(&g, &t, 2000, R5[i] * 2.0f, 30, 60000);
        feed(&g, &t, 900, 0, 0, 1000); /* separa os regimes */
    }
    gear_input_t in = mk(t, 2000, 40, 30);
    gear_state_t s = gear_engine_update(&g, &in);
    TEST_ASSERT_FALSE(s.learning);
    TEST_ASSERT_EQUAL_UINT8(5, s.learned_gears);
    TEST_ASSERT_TRUE(gear_engine_take_learned(&g, out));
    for (int i = 0; i < 5; i++) TEST_ASSERT_FLOAT_WITHIN(R5[i] * 0.04f, R5[i], out[i]);
    TEST_ASSERT_FALSE(gear_engine_take_learned(&g, out));
    s = feed(&g, &t, 2500, 20.0f * 2.5f, 30, 1000);
    TEST_ASSERT_EQUAL_UINT8(3, s.gear);
}

static void test_preloaded_ratios_start_converged(void)
{
    gear_engine_t g;
    float out[VP_MAX_GEARS];
    uint32_t t = 0;
    gear_engine_init(&g, vp_find("generic_man"), R5);
    TEST_ASSERT_FALSE(gear_engine_take_learned(&g, out));
    gear_state_t s = feed(&g, &t, 2000, 40, 30, 1000);
    TEST_ASSERT_FALSE(s.learning);
    TEST_ASSERT_EQUAL_UINT8(5, s.learned_gears);
    TEST_ASSERT_EQUAL_UINT8(3, s.gear);
}

static void test_reset_learning_returns_to_seeds(void)
{
    gear_engine_t g;
    uint32_t t = 0;
    gear_engine_init(&g, vp_find("generic_man"), R5);
    gear_engine_reset_learning(&g);
    gear_state_t s = feed(&g, &t, 2000, 40, 30, 1000);
    TEST_ASSERT_TRUE(s.learning);
    TEST_ASSERT_EQUAL_UINT8(0, s.learned_gears);
    TEST_ASSERT_EQUAL_UINT8(0, s.gear);
}

static void test_six_gear_profile(void)
{
    gear_engine_t g;
    uint32_t t = 0;
    gear_engine_init(&g, &test_six_gear, NULL);
    /* 6ª: 35.0 km/h por 1000 RPM; 3000 RPM -> 105 km/h */
    gear_state_t s = feed(&g, &t, 3000, 105, 15, 2000);
    TEST_ASSERT_EQUAL_UINT8(6, s.gear);
    TEST_ASSERT_EQUAL(SHIFT_NONE, s.advice);
    TEST_ASSERT_TRUE(s.learning);
    TEST_ASSERT_EQUAL_UINT8(6, s.learned_gears);
}

static void check_invalid_learned(const float *bad)
{
    gear_engine_t g;
    float out[VP_MAX_GEARS];
    uint32_t t = 0;
    gear_engine_init(&g, vp_find("generic_man"), bad);
    /* 3ª a 2200 RPM / 44 km/h: com relações válidas daria UP */
    gear_state_t s = feed(&g, &t, 2200, 44, 15, 1000);
    TEST_ASSERT_TRUE(s.learning);
    TEST_ASSERT_EQUAL_UINT8(0, s.learned_gears);
    TEST_ASSERT_EQUAL(SHIFT_NONE, s.advice);
    TEST_ASSERT_EQUAL_UINT8(0, s.target_gear);
    TEST_ASSERT_FALSE(gear_engine_take_learned(&g, out));
}

static void test_invalid_learned_is_ignored(void)
{
    const float with_zero[5] = {7.5f, 0.0f, 20.0f, 27.1f, 36.9f};
    const float flat[5] = {7.5f, 13.6f, 13.6f, 27.1f, 36.9f};
    const float with_nan[5] = {7.5f, NAN, 20.0f, 27.1f, 36.9f};
    check_invalid_learned(with_zero);
    check_invalid_learned(flat);
    check_invalid_learned(with_nan);
}

void run_gear_engine_tests(void)
{
    RUN_TEST(test_stale_clears);
    RUN_TEST(test_first_boot_no_seeds);
    RUN_TEST(test_learns_then_reports_once);
    RUN_TEST(test_preloaded_ratios_start_converged);
    RUN_TEST(test_reset_learning_returns_to_seeds);
    RUN_TEST(test_six_gear_profile);
    RUN_TEST(test_invalid_learned_is_ignored);
}
