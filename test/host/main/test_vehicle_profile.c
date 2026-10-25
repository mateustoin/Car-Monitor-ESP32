#include <string.h>
#include "unity.h"
#include "vehicle_profile.h"
#include "test_profiles.h"

const vehicle_profile_t test_six_gear = {
    .id = "test_six", .name = "Teste 6 marchas",
    .gears = 6, .fuel = VP_FUEL_GASOLINE,
    .seed_ratios = { 7.0f, 12.0f, 17.5f, 23.0f, 29.0f, 35.0f },
    .idle_rpm = 800, .redline_rpm = 6000,
    .up_rpm_light = 2100, .up_rpm_heavy = 2800,
    .load_light_pct = 20, .load_heavy_pct = 80,
    .min_rpm_after_upshift = 1500,
    .lug_rpm = 1300, .lug_load_pct = 60,
    .target_band_low = 2000, .target_band_high = 2500,
    .ratio_tolerance = 0.07f,
    .gear_hold_ms = 400, .stale_ms = 1000,
};

static void test_find_default(void)
{
    const vehicle_profile_t *p = vp_default();
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("sandero_k7m", p->id);
    TEST_ASSERT_EQUAL(5, p->gears);
    TEST_ASSERT_EQUAL_PTR(p, vp_find("sandero_k7m"));
}

static void test_find_unknown(void)
{
    TEST_ASSERT_NULL(vp_find("nao_existe"));
    TEST_ASSERT_NULL(vp_find(NULL));
}

static void test_all_builtin_valid(void)
{
    TEST_ASSERT_TRUE(vp_count() >= 2);
    for (size_t i = 0; i < vp_count(); i++) {
        TEST_ASSERT_TRUE(vp_validate(vp_get(i)));
    }
    TEST_ASSERT_NULL(vp_get(vp_count()));
    TEST_ASSERT_NOT_NULL(vp_find("generic_man"));
    TEST_ASSERT_EQUAL(VP_FUEL_GASOLINE, vp_find("generic_man")->fuel);
}

static void test_six_gear_valid(void)
{
    TEST_ASSERT_TRUE(vp_validate(&test_six_gear));
}

static void test_validate_rejects(void)
{
    vehicle_profile_t p;

    TEST_ASSERT_FALSE(vp_validate(NULL));

    p = test_six_gear; p.up_rpm_light = 3000; p.up_rpm_heavy = 2800;
    TEST_ASSERT_FALSE(vp_validate(&p));
    p = test_six_gear; p.up_rpm_light = p.up_rpm_heavy;
    TEST_ASSERT_FALSE(vp_validate(&p));
    p = test_six_gear; p.gears = 0;
    TEST_ASSERT_FALSE(vp_validate(&p));
    p = test_six_gear; p.gears = VP_MAX_GEARS + 1;
    TEST_ASSERT_FALSE(vp_validate(&p));
    p = test_six_gear; p.id = "treze_chars_x";
    TEST_ASSERT_FALSE(vp_validate(&p));
    p = test_six_gear; p.id = "";
    TEST_ASSERT_FALSE(vp_validate(&p));
    p = test_six_gear; p.id = NULL;
    TEST_ASSERT_FALSE(vp_validate(&p));
    p = test_six_gear; p.id = "doze_chars_x";  /* 12 chars: aceito */
    TEST_ASSERT_TRUE(vp_validate(&p));
    p = test_six_gear; p.name = NULL;
    TEST_ASSERT_FALSE(vp_validate(&p));
    p = test_six_gear; p.target_band_low = 2500; p.target_band_high = 2000;
    TEST_ASSERT_FALSE(vp_validate(&p));
    p = test_six_gear; p.idle_rpm = 6000;
    TEST_ASSERT_FALSE(vp_validate(&p));
    p = test_six_gear; p.target_band_high = 6500;
    TEST_ASSERT_FALSE(vp_validate(&p));
    p = test_six_gear; p.load_light_pct = 80;
    TEST_ASSERT_FALSE(vp_validate(&p));
    p = test_six_gear; p.load_heavy_pct = 101;
    TEST_ASSERT_FALSE(vp_validate(&p));
    p = test_six_gear; p.ratio_tolerance = 0.0f;
    TEST_ASSERT_FALSE(vp_validate(&p));
    p = test_six_gear; p.ratio_tolerance = 0.5f;
    TEST_ASSERT_FALSE(vp_validate(&p));
    p = test_six_gear; p.seed_ratios[2] = 11.0f;  /* não crescente */
    TEST_ASSERT_FALSE(vp_validate(&p));
}

void run_vehicle_profile_tests(void)
{
    RUN_TEST(test_find_default);
    RUN_TEST(test_find_unknown);
    RUN_TEST(test_all_builtin_valid);
    RUN_TEST(test_six_gear_valid);
    RUN_TEST(test_validate_rejects);
}
