#include <string.h>
#include "unity.h"
#include "obd_poller.h"
#include "elm327.h"

static bool all_ok(void *arg, uint8_t pid) { (void)arg; (void)pid; return true; }
static bool no_throttle(void *arg, uint8_t pid) { (void)arg; return pid != ELM_PID_THROTTLE; }
static bool no_load(void *arg, uint8_t pid) { (void)arg; return pid != ELM_PID_LOAD; }
static bool only_rpm_speed(void *arg, uint8_t pid)
{
    (void)arg;
    return pid == ELM_PID_RPM || pid == ELM_PID_SPEED;
}

static void test_poller_fast_round_robin(void)
{
    obd_poller_t p;
    obd_poller_init(&p, all_ok, NULL);
    int c0c = 0, c0d = 0, c05 = 0;
    for (int i = 0; i < 20; i++) {
        uint8_t pid = obd_poller_next(&p, (uint32_t)i * 50);
        TEST_ASSERT_TRUE(pid == ELM_PID_RPM || pid == ELM_PID_SPEED ||
                         pid == ELM_PID_LOAD || pid == ELM_PID_THROTTLE ||
                         pid == ELM_PID_MAP || pid == ELM_PID_IAT);
    }
    obd_poller_init(&p, all_ok, NULL);
    for (uint32_t t = 0; t <= 10000; t += 50) {
        uint8_t pid = obd_poller_next(&p, t);
        if (pid == ELM_PID_RPM) c0c++;
        if (pid == ELM_PID_SPEED) c0d++;
        if (pid == ELM_PID_COOLANT) c05++;
    }
    TEST_ASSERT_TRUE(c05 >= 1);
    TEST_ASSERT_TRUE(c0c >= 4 * c05);
    TEST_ASSERT_TRUE(c0d >= 4 * c05);
}

static bool core_plus_coolant(void *arg, uint8_t pid)
{
    (void)arg;
    return pid == ELM_PID_RPM || pid == ELM_PID_SPEED || pid == ELM_PID_COOLANT;
}

static void test_poller_slow_when_due(void)
{
    obd_poller_t p;
    obd_poller_init(&p, core_plus_coolant, NULL);
    for (uint32_t t = 0; t < 5000; t += 50)
        TEST_ASSERT_TRUE(obd_poller_next(&p, t) != ELM_PID_COOLANT);
    TEST_ASSERT_EQUAL_UINT8(ELM_PID_COOLANT, obd_poller_next(&p, 5000));
    /* depois de pedido, só volta a vencer 5000 ms depois */
    TEST_ASSERT_TRUE(obd_poller_next(&p, 5050) != ELM_PID_COOLANT);
}

static void test_poller_slow_overdue_most_wins(void)
{
    obd_poller_t p;
    obd_poller_init(&p, all_ok, NULL);
    obd_poller_next(&p, 0);
    /* em 5000: 05 atrasado 0 ms, 0B/0F atrasados 4000 ms -> 0B/0F primeiro (ordem da lista), 05 por último */
    uint8_t a = obd_poller_next(&p, 5000);
    uint8_t b = obd_poller_next(&p, 5000);
    uint8_t c = obd_poller_next(&p, 5000);
    TEST_ASSERT_TRUE(a == ELM_PID_MAP || a == ELM_PID_IAT);
    TEST_ASSERT_TRUE(b == ELM_PID_MAP || b == ELM_PID_IAT);
    TEST_ASSERT_NOT_EQUAL(a, b);
    TEST_ASSERT_EQUAL_UINT8(ELM_PID_COOLANT, c);
}

static void test_poller_fallback_no_throttle(void)
{
    obd_poller_t p;
    obd_poller_init(&p, no_throttle, NULL);
    for (uint32_t t = 0; t < 20000; t += 50)
        TEST_ASSERT_TRUE(obd_poller_next(&p, t) != ELM_PID_THROTTLE);
    TEST_ASSERT_EQUAL_UINT8(ELM_PID_LOAD, obd_poller_snapshot(&p)->load_source_pid);
}

static void test_poller_fallback_no_load(void)
{
    obd_poller_t p;
    obd_poller_init(&p, no_load, NULL);
    TEST_ASSERT_EQUAL_UINT8(ELM_PID_THROTTLE, obd_poller_snapshot(&p)->load_source_pid);
    int n11 = 0, n04 = 0, nfast = 0;
    for (uint32_t t = 0; t < 900; t += 50) {
        uint8_t pid = obd_poller_next(&p, t);
        if (pid == ELM_PID_THROTTLE) n11++;
        if (pid == ELM_PID_LOAD) n04++;
        nfast++;
    }
    TEST_ASSERT_EQUAL_INT(0, n04);
    TEST_ASSERT_EQUAL_INT(nfast / 3 + (nfast % 3 ? 1 : 0), n11); /* lista 0C,0D,11: sem 11 duplicado */
    obd_poller_record(&p, ELM_PID_THROTTLE, 42.0f, true, 100);
    const obd_snapshot_t *s = obd_poller_snapshot(&p);
    TEST_ASSERT_TRUE(s->load_pct.valid);
    TEST_ASSERT_EQUAL_FLOAT(42.0f, s->load_pct.value);
    TEST_ASSERT_TRUE(s->throttle_pct.valid);
    TEST_ASSERT_EQUAL_FLOAT(42.0f, s->throttle_pct.value);
}

static void test_poller_load_not_from_throttle_when_04(void)
{
    obd_poller_t p;
    obd_poller_init(&p, all_ok, NULL);
    obd_poller_record(&p, ELM_PID_THROTTLE, 30.0f, true, 100);
    TEST_ASSERT_FALSE(obd_poller_snapshot(&p)->load_pct.valid);
    TEST_ASSERT_TRUE(obd_poller_snapshot(&p)->throttle_pct.valid);
    obd_poller_record(&p, ELM_PID_LOAD, 55.0f, true, 120);
    TEST_ASSERT_EQUAL_FLOAT(55.0f, obd_poller_snapshot(&p)->load_pct.value);
}

static void test_poller_neither_load_nor_throttle(void)
{
    obd_poller_t p;
    obd_poller_init(&p, only_rpm_speed, NULL);
    TEST_ASSERT_EQUAL_UINT8(0, obd_poller_snapshot(&p)->load_source_pid);
    TEST_ASSERT_FALSE(obd_poller_snapshot(&p)->load_pct.valid);
    for (uint32_t t = 0; t < 20000; t += 50) {
        uint8_t pid = obd_poller_next(&p, t);
        TEST_ASSERT_TRUE(pid == ELM_PID_RPM || pid == ELM_PID_SPEED);
    }
}

static bool nothing(void *arg, uint8_t pid) { (void)arg; (void)pid; return false; }

static void test_poller_rpm_speed_always_scheduled(void)
{
    obd_poller_t p;
    obd_poller_init(&p, nothing, NULL);
    uint8_t a = obd_poller_next(&p, 0), b = obd_poller_next(&p, 50);
    TEST_ASSERT_TRUE((a == ELM_PID_RPM && b == ELM_PID_SPEED) || (a == ELM_PID_SPEED && b == ELM_PID_RPM));
}

static void test_poller_record_and_rate(void)
{
    obd_poller_t p;
    obd_poller_init(&p, all_ok, NULL);
    for (int i = 0; i < 10; i++)
        obd_poller_record(&p, ELM_PID_RPM, 1000.0f + (float)i, true, 1000 + (uint32_t)i * 100);
    const obd_snapshot_t *s = obd_poller_snapshot(&p);
    TEST_ASSERT_TRUE(s->rate_hz >= 8.0f && s->rate_hz <= 12.0f);
    TEST_ASSERT_TRUE(s->rpm.valid);
    TEST_ASSERT_EQUAL_FLOAT(1009.0f, s->rpm.value);
    TEST_ASSERT_EQUAL_UINT32(1900, s->rpm.t_ms);
    float rate = s->rate_hz;
    obd_poller_record(&p, ELM_PID_RPM, 5.0f, false, 2000);
    TEST_ASSERT_EQUAL_FLOAT(1009.0f, s->rpm.value);
    TEST_ASSERT_EQUAL_UINT32(1900, s->rpm.t_ms);
    TEST_ASSERT_EQUAL_FLOAT(rate, s->rate_hz);
}

static void test_poller_record_maps_pids(void)
{
    obd_poller_t p;
    obd_poller_init(&p, all_ok, NULL);
    obd_poller_record(&p, ELM_PID_SPEED, 50.0f, true, 10);
    obd_poller_record(&p, ELM_PID_MAP, 90.0f, true, 20);
    obd_poller_record(&p, ELM_PID_COOLANT, 88.0f, true, 30);
    obd_poller_record(&p, ELM_PID_IAT, 31.0f, true, 40);
    const obd_snapshot_t *s = obd_poller_snapshot(&p);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, s->speed_kmh.value);
    TEST_ASSERT_EQUAL_FLOAT(90.0f, s->map_kpa.value);
    TEST_ASSERT_EQUAL_FLOAT(88.0f, s->coolant_c.value);
    TEST_ASSERT_EQUAL_FLOAT(31.0f, s->iat_c.value);
    TEST_ASSERT_EQUAL_UINT32(40, s->iat_c.t_ms);
}

void run_obd_poller_tests(void)
{
    RUN_TEST(test_poller_fast_round_robin);
    RUN_TEST(test_poller_slow_when_due);
    RUN_TEST(test_poller_slow_overdue_most_wins);
    RUN_TEST(test_poller_fallback_no_throttle);
    RUN_TEST(test_poller_fallback_no_load);
    RUN_TEST(test_poller_load_not_from_throttle_when_04);
    RUN_TEST(test_poller_neither_load_nor_throttle);
    RUN_TEST(test_poller_rpm_speed_always_scheduled);
    RUN_TEST(test_poller_record_and_rate);
    RUN_TEST(test_poller_record_maps_pids);
}
