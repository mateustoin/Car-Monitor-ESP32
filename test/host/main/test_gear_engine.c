#include <math.h>
#include "unity.h"
#include "gear_engine.h"
#include "test_profiles.h"
#include "ui_logic.h"

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
    feed(&g, &t, 2000, 40, 30, 1000);
    TEST_ASSERT_TRUE(g.last_ratio > 0);
    gear_engine_reset_learning(&g);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, g.last_ratio);   /* amostra anterior ao reset não conta como estável */
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

/* como feed, com acelerador informado */
static gear_state_t feed_thr(gear_engine_t *g, uint32_t *t, float rpm, float spd, float load, float thr,
                             uint32_t dur_ms, int *downs)
{
    gear_state_t s = {0};
    for (uint32_t e = 0; e < dur_ms; e += 100) {
        gear_input_t in = mk(*t, rpm, spd, load);
        in.throttle_pct = thr;
        s = gear_engine_update(g, &in);
        if (downs && s.advice == SHIFT_DOWN) (*downs)++;
        *t += 100;
    }
    return s;
}

static void test_resting_throttle_coast_no_down(void)
{
    /* R30: ECU com 0x11 em 12% de pé fora. 5ª a 1200 RPM com carga alta: com o acelerador
       no repouso (12%) é desaceleração, não pedido de força -> nunca DOWN. */
    gear_engine_t g;
    uint32_t t = 0;
    int downs = 0;
    gear_engine_init(&g, vp_default(), R5);
    gear_state_t s = feed_thr(&g, &t, 1200, 1.2f * R5[4], 80, 12, 3000, &downs);
    TEST_ASSERT_EQUAL_UINT8(5, s.gear);
    TEST_ASSERT_EQUAL(0, downs);
    /* pisando (12 -> 40%) a redução volta a ser sugerida */
    s = feed_thr(&g, &t, 1200, 1.2f * R5[4], 80, 40, 2000, &downs);
    TEST_ASSERT_EQUAL(SHIFT_DOWN, s.advice);
}

static void test_resting_throttle_not_applied_for_learning(void)
{
    /* Carga baixa (5%) e acelerador em repouso (12%): não é amostra estável. */
    gear_engine_t g;
    uint32_t t = 0;
    gear_engine_init(&g, vp_find("generic_man"), NULL);
    gear_state_t s = feed_thr(&g, &t, 2000, 40, 5, 12, 60000, NULL);
    TEST_ASSERT_EQUAL_UINT8(0, s.learned_gears);
    /* 20% fica > repouso + 3 -> aplicado: o aprendiz passa a contar */
    s = feed_thr(&g, &t, 2000, 40, 5, 20, 60000, NULL);
    TEST_ASSERT_EQUAL_UINT8(1, s.learned_gears);
}

static void test_resting_throttle_capped(void)
{
    /* Mínimo saturado em 25%: um acelerador sempre em 40% não vira "fechado". */
    gear_engine_t g;
    uint32_t t = 0;
    gear_engine_init(&g, vp_default(), R5);
    gear_state_t s = feed_thr(&g, &t, 1200, 1.2f * R5[4], 80, 40, 3000, NULL);
    TEST_ASSERT_EQUAL(SHIFT_DOWN, s.advice);
}

static void test_up_hysteresis_single_tone(void)
{
    /* R33: 3ª, carga 15 (limite 2100). RPM oscila ±80 em torno do limite por 20 s a 10 Hz
       (período de 4 s): o conselho aparece uma vez, não pisca UP/NONE, e só um bipe. */
    gear_engine_t g;
    ui_alert_t a;
    uint32_t t = 0;
    gear_engine_init(&g, vp_default(), R5);
    ui_alert_init(&a);
    gear_state_t s = feed(&g, &t, 1900, 1.9f * R5[2], 15, 2000);
    TEST_ASSERT_EQUAL_UINT8(3, s.gear);
    TEST_ASSERT_EQUAL(SHIFT_NONE, s.advice);
    int rises = 0, falls = 0, tones = 0;
    shift_advice_t prev = SHIFT_NONE;
    for (int i = 0; i < 200; i++) {
        float rpm = 2100.0f + 80.0f * sinf(2.0f * (float)M_PI * (float)i / 40.0f);
        gear_input_t in = mk(t, rpm, rpm * R5[2] / 1000.0f, 15);
        s = gear_engine_update(&g, &in);
        TEST_ASSERT_EQUAL_UINT8(3, s.gear);
        TEST_ASSERT_TRUE(s.advice != SHIFT_DOWN);
        if (prev == SHIFT_NONE && s.advice == SHIFT_UP) rises++;
        if (prev == SHIFT_UP && s.advice == SHIFT_NONE) falls++;
        if (ui_alert_update(&a, s.advice, s.gear, t, false) != UI_TONE_NONE) tones++;
        prev = s.advice;
        t += 100;
    }
    TEST_ASSERT_EQUAL(1, rises);
    TEST_ASSERT_EQUAL(0, falls);
    TEST_ASSERT_EQUAL(1, tones);
    TEST_ASSERT_EQUAL(SHIFT_UP, s.advice);
    TEST_ASSERT_EQUAL_UINT8(4, s.target_gear);
    /* só abaixo de limite - 150 o conselho some */
    s = feed(&g, &t, 1960, 1.96f * R5[2], 15, 1000);
    TEST_ASSERT_EQUAL(SHIFT_UP, s.advice);
    s = feed(&g, &t, 1940, 1.94f * R5[2], 15, 200);
    TEST_ASSERT_EQUAL(SHIFT_NONE, s.advice);
}

static void test_no_advice_while_detector_unsettled(void)
{
    /* R33: 4ª exibida, embreagem pisada e giro em 3000 (relação não casa): enquanto a marcha
       exibida ainda é a 4ª (hold), nada de UP. Hold maior que o dwell (como em taxa baixa)
       para o teste não depender só do dwell. */
    vehicle_profile_t p = *vp_default();
    p.gear_hold_ms = 1000;
    gear_engine_t g;
    uint32_t t = 0;
    gear_engine_init(&g, &p, R5);
    gear_state_t s = feed(&g, &t, 2000, 2.0f * R5[3], 15, 3000);
    TEST_ASSERT_EQUAL_UINT8(4, s.gear);
    TEST_ASSERT_EQUAL(SHIFT_NONE, s.advice);
    for (int i = 0; i < 9; i++) {   /* 900 ms < hold: a 4ª continua na tela */
        gear_input_t in = mk(t, 3000, 2.0f * R5[3], 15);
        s = gear_engine_update(&g, &in);
        TEST_ASSERT_EQUAL_UINT8(4, s.gear);
        TEST_ASSERT_EQUAL(SHIFT_NONE, s.advice);
        TEST_ASSERT_EQUAL_UINT8(0, s.target_gear);
        t += 100;
    }
}

static void test_downshift_after_dwell(void)
{
    /* 5ª a 1200 RPM pedindo força: a 5ª aparece em 400 ms (hold) e o DOWN 400 ms depois (dwell). */
    gear_engine_t g;
    uint32_t t = 0;
    gear_engine_init(&g, vp_default(), R5);
    gear_state_t s = feed_thr(&g, &t, 1200, 1.2f * R5[4], 80, 40, 800, NULL);   /* t = 0..700 */
    TEST_ASSERT_EQUAL_UINT8(5, s.gear);
    TEST_ASSERT_EQUAL(SHIFT_NONE, s.advice);
    s = feed_thr(&g, &t, 1200, 1.2f * R5[4], 80, 40, 100, NULL);                 /* t = 800 */
    TEST_ASSERT_EQUAL(SHIFT_DOWN, s.advice);
    TEST_ASSERT_EQUAL_UINT8(3, s.target_gear);
    /* faixa de saída: até lug + 150 (1450) continua, mesmo com a carga caindo */
    s = feed_thr(&g, &t, 1400, 1.4f * R5[4], 40, 40, 500, NULL);
    TEST_ASSERT_EQUAL(SHIFT_DOWN, s.advice);
    s = feed_thr(&g, &t, 1500, 1.5f * R5[4], 40, 40, 100, NULL);
    TEST_ASSERT_EQUAL(SHIFT_NONE, s.advice);
}

static void test_downshift_clears_when_throttle_closes(void)
{
    gear_engine_t g;
    uint32_t t = 0;
    gear_engine_init(&g, vp_default(), R5);
    gear_state_t s = feed_thr(&g, &t, 1200, 1.2f * R5[4], 80, 40, 2000, NULL);
    TEST_ASSERT_EQUAL(SHIFT_DOWN, s.advice);
    s = feed_thr(&g, &t, 1200, 1.2f * R5[4], 80, 0, 100, NULL);   /* tirou o pé: injeção cortada */
    TEST_ASSERT_EQUAL(SHIFT_NONE, s.advice);
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
    RUN_TEST(test_resting_throttle_coast_no_down);
    RUN_TEST(test_resting_throttle_not_applied_for_learning);
    RUN_TEST(test_resting_throttle_capped);
    RUN_TEST(test_up_hysteresis_single_tone);
    RUN_TEST(test_no_advice_while_detector_unsettled);
    RUN_TEST(test_downshift_after_dwell);
    RUN_TEST(test_downshift_clears_when_throttle_closes);
}
