#include "unity.h"
#include "ui_logic.h"

static bool inside(ui_rect_t r, int w, int h)
{
    return r.w > 0 && r.h > 0 && r.x >= 0 && r.y >= 0 && r.x + r.w <= w && r.y + r.h <= h;
}

static bool overlap(ui_rect_t a, ui_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void check_layout(uint16_t w, uint16_t h)
{
    ui_layout_t l = ui_layout_compute(w, h);
    TEST_ASSERT_TRUE(inside(l.gear, w, h));
    TEST_ASSERT_TRUE(inside(l.target, w, h));
    TEST_ASSERT_TRUE(inside(l.status, w, h));
    TEST_ASSERT_TRUE(l.gear.h * 10 >= h * 6);
    TEST_ASSERT_FALSE(overlap(l.gear, l.target));
    TEST_ASSERT_FALSE(overlap(l.gear, l.status));
    TEST_ASSERT_FALSE(overlap(l.target, l.status));
    TEST_ASSERT_TRUE(l.gear_font_px > 0 && l.gear_font_px <= l.gear.h);
    if (w >= h) {
        TEST_ASSERT_TRUE(l.target.x >= l.gear.x + l.gear.w);   /* à direita */
    } else {
        TEST_ASSERT_TRUE(l.target.y >= l.gear.y + l.gear.h);   /* abaixo */
    }
}

static void test_layout_fits_all(void)
{
    check_layout(240, 135);
    check_layout(320, 240);
    check_layout(135, 240);
    check_layout(480, 320);
}

static void test_layout_font_proportional(void)
{
    ui_layout_t a = ui_layout_compute(240, 135), b = ui_layout_compute(480, 270);
    TEST_ASSERT_UINT16_WITHIN(2, a.gear_font_px * 2, b.gear_font_px);
}

static void test_bg_mapping(void)
{
    gear_state_t s = {.gear = 3, .advice = SHIFT_UP, .target_gear = 4};
    TEST_ASSERT_EQUAL(UI_BG_UP, ui_bg_for(&s, true));
    s.advice = SHIFT_DOWN;
    TEST_ASSERT_EQUAL(UI_BG_DOWN, ui_bg_for(&s, true));
    TEST_ASSERT_EQUAL(UI_BG_NODATA, ui_bg_for(&s, false));
    s.stale = true;
    TEST_ASSERT_EQUAL(UI_BG_NODATA, ui_bg_for(&s, true));
    s.stale = false;
    s.advice = SHIFT_NONE;
    TEST_ASSERT_EQUAL(UI_BG_OK, ui_bg_for(&s, true));
    TEST_ASSERT_EQUAL(UI_BG_NODATA, ui_bg_for(NULL, true));
}

static void test_bg_rgb(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x1B8A3A, ui_bg_rgb(UI_BG_OK));
    TEST_ASSERT_EQUAL_HEX32(0x1565C0, ui_bg_rgb(UI_BG_UP));
    TEST_ASSERT_EQUAL_HEX32(0xEF6C00, ui_bg_rgb(UI_BG_DOWN));
    TEST_ASSERT_EQUAL_HEX32(0x424242, ui_bg_rgb(UI_BG_NODATA));
}

static void test_alert_once(void)
{
    ui_alert_t a;
    ui_alert_init(&a);
    int tones = 0;
    for (uint32_t t = 0; t < 10000; t += 100) {
        ui_tone_t r = ui_alert_update(&a, SHIFT_UP, t, false);
        if (r == UI_TONE_UP) tones++;
        else TEST_ASSERT_EQUAL(UI_TONE_NONE, r);
    }
    TEST_ASSERT_EQUAL(1, tones);
}

static void test_alert_min_interval(void)
{
    ui_alert_t a;
    ui_alert_init(&a);
    TEST_ASSERT_EQUAL(UI_TONE_UP, ui_alert_update(&a, SHIFT_UP, 5000, false));
    TEST_ASSERT_EQUAL(UI_TONE_NONE, ui_alert_update(&a, SHIFT_NONE, 5300, false));
    TEST_ASSERT_EQUAL(UI_TONE_NONE, ui_alert_update(&a, SHIFT_UP, 6000, false));  /* < 3 s: suprimido */
    TEST_ASSERT_EQUAL(UI_TONE_NONE, ui_alert_update(&a, SHIFT_UP, 9000, false));  /* sem nova transição */
    TEST_ASSERT_EQUAL(UI_TONE_NONE, ui_alert_update(&a, SHIFT_NONE, 9100, false));
    TEST_ASSERT_EQUAL(UI_TONE_UP, ui_alert_update(&a, SHIFT_UP, 9200, false));    /* >= 3 s do último bipe */
}

static void test_alert_direct_swap(void)
{
    ui_alert_t a;
    ui_alert_init(&a);
    TEST_ASSERT_EQUAL(UI_TONE_UP, ui_alert_update(&a, SHIFT_UP, 0, false));
    TEST_ASSERT_EQUAL(UI_TONE_DOWN, ui_alert_update(&a, SHIFT_DOWN, 3000, false));
    TEST_ASSERT_EQUAL(UI_TONE_NONE, ui_alert_update(&a, SHIFT_UP, 4000, false));  /* troca direta suprimida */
}

static void test_alert_muted(void)
{
    ui_alert_t a;
    ui_alert_init(&a);
    TEST_ASSERT_EQUAL(UI_TONE_NONE, ui_alert_update(&a, SHIFT_UP, 0, true));
    TEST_ASSERT_EQUAL(UI_TONE_NONE, ui_alert_update(&a, SHIFT_DOWN, 5000, true));
    TEST_ASSERT_EQUAL(UI_TONE_NONE, ui_alert_update(&a, SHIFT_NONE, 9000, true));
    TEST_ASSERT_EQUAL(UI_TONE_NONE, ui_alert_update(&a, SHIFT_UP, 20000, true));
}

void run_ui_logic_tests(void)
{
    RUN_TEST(test_layout_fits_all);
    RUN_TEST(test_layout_font_proportional);
    RUN_TEST(test_bg_mapping);
    RUN_TEST(test_bg_rgb);
    RUN_TEST(test_alert_once);
    RUN_TEST(test_alert_min_interval);
    RUN_TEST(test_alert_direct_swap);
    RUN_TEST(test_alert_muted);
}
