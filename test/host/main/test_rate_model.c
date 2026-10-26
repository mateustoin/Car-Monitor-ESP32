/* R34: agenda, stale e hold seguem a taxa medida (ELM lento / K-line). */
#include <math.h>
#include "unity.h"
#include "elm327.h"
#include "gear_engine.h"
#include "obd_poller.h"

static const float R5[5] = {7.5f, 13.6f, 20.0f, 27.1f, 36.9f};

static float cruise_value(uint8_t pid)
{
    switch (pid) {
    case ELM_PID_RPM: return 2000.0f;
    case ELM_PID_SPEED: return 40.0f;      /* 3ª: 20 km/h por 1000 RPM */
    case ELM_PID_LOAD: return 30.0f;
    case ELM_PID_THROTTLE: return 15.0f;
    case ELM_PID_COOLANT: return 90.0f;
    default: return 0.0f;
    }
}

static void test_slow_elm_keeps_gear(void)
{
    /* ELM a 4 Hz (uma resposta a cada 250 ms) e o laço da UI a 20 Hz, como no app: o
       gear_engine roda com amostra nova ou quando a antiga envelhece. Cruzeiro estável na 3ª
       por 60 s: depois do aquecimento, a marcha fica na tela e nunca vira stale. */
    obd_poller_t p;
    gear_engine_t g;
    obd_poller_init(&p, NULL, NULL, false);
    gear_engine_init(&g, vp_default(), R5);
    uint32_t next_resp = 0, last_rpm_t = UINT32_MAX, last_spd_t = UINT32_MAX;
    int checked = 0;
    for (uint32_t now = 0; now < 60000; now += 50) {
        if (now >= next_resp) {
            uint8_t pid = obd_poller_next(&p, now);
            obd_poller_record(&p, pid, cruise_value(pid), true, now);
            next_resp = now + 250;
        }
        const obd_snapshot_t *s = obd_poller_snapshot(&p);
        if (!s->rpm.valid || !s->speed_kmh.valid) continue;
        gear_engine_set_poll_cycle(&g, s->fast_cycle_ms);
        uint32_t sample_ms = s->rpm.t_ms < s->speed_kmh.t_ms ? s->rpm.t_ms : s->speed_kmh.t_ms;
        bool fresh = s->rpm.t_ms != last_rpm_t || s->speed_kmh.t_ms != last_spd_t;
        if (!fresh && !gear_sample_is_stale(now, sample_ms, gear_engine_stale_ms(&g))) continue;
        last_rpm_t = s->rpm.t_ms;
        last_spd_t = s->speed_kmh.t_ms;
        gear_input_t in = {.now_ms = now, .sample_ms = sample_ms, .rpm = s->rpm.value,
                           .speed_kmh = s->speed_kmh.value, .load_pct = s->load_pct.value,
                           .throttle_pct = s->throttle_pct.value};
        gear_state_t st = gear_engine_update(&g, &in);
        if (now < 10000) continue;
        TEST_ASSERT_FALSE(st.stale);
        TEST_ASSERT_EQUAL_UINT8(3, st.gear);
        checked++;
    }
    TEST_ASSERT_TRUE(checked >= 80);   /* ~2 amostras novas (0C, 0D) por segundo por 50 s */
    /* 4 itens rápidos a 4 Hz: ciclo ~1 s -> stale >= 2,5 s e hold >= 2 s */
    TEST_ASSERT_TRUE(gear_engine_stale_ms(&g) >= 2400);
    TEST_ASSERT_TRUE(gear_engine_hold_ms(&g) >= 1900);
}

static void test_effective_timing(void)
{
    gear_engine_t g;
    const vehicle_profile_t *prof = vp_default();
    gear_engine_init(&g, prof, R5);
    TEST_ASSERT_EQUAL_UINT32(prof->stale_ms, gear_engine_stale_ms(&g));
    TEST_ASSERT_EQUAL_UINT32(prof->gear_hold_ms, gear_engine_hold_ms(&g));
    gear_engine_set_poll_cycle(&g, 100);    /* ELM rápido: valem os do perfil */
    TEST_ASSERT_EQUAL_UINT32(prof->stale_ms, gear_engine_stale_ms(&g));
    TEST_ASSERT_EQUAL_UINT32(prof->gear_hold_ms, gear_engine_hold_ms(&g));
    gear_engine_set_poll_cycle(&g, 1000);
    TEST_ASSERT_EQUAL_UINT32(2500, gear_engine_stale_ms(&g));
    TEST_ASSERT_EQUAL_UINT32(2000, gear_engine_hold_ms(&g));
    gear_engine_set_poll_cycle(&g, 0);      /* taxa desconhecida: volta ao perfil */
    TEST_ASSERT_EQUAL_UINT32(prof->stale_ms, gear_engine_stale_ms(&g));
    TEST_ASSERT_EQUAL_UINT32(prof->gear_hold_ms, gear_engine_hold_ms(&g));
}

static void test_hold_scales_up(void)
{
    /* Com ciclo de 1 s, a marcha nova só aparece depois de 2 s estável (não 400 ms). */
    gear_engine_t g;
    gear_engine_init(&g, vp_default(), R5);
    gear_engine_set_poll_cycle(&g, 1000);
    gear_state_t s = {0};
    for (uint32_t t = 0; t <= 1900; t += 100) {
        gear_input_t in = {.now_ms = t, .sample_ms = t, .rpm = 2000, .speed_kmh = 40,
                           .load_pct = 30, .throttle_pct = 15};
        s = gear_engine_update(&g, &in);
        TEST_ASSERT_EQUAL_UINT8(0, s.gear);
    }
    gear_input_t in = {.now_ms = 2000, .sample_ms = 2000, .rpm = 2000, .speed_kmh = 40,
                       .load_pct = 30, .throttle_pct = 15};
    s = gear_engine_update(&g, &in);
    TEST_ASSERT_EQUAL_UINT8(3, s.gear);
}

void run_rate_model_tests(void)
{
    RUN_TEST(test_slow_elm_keeps_gear);
    RUN_TEST(test_effective_timing);
    RUN_TEST(test_hold_scales_up);
}
