#include <math.h>
#include "unity.h"
#include "power_mgr.h"

#define MIN_MS (60u * 1000u)

static void test_running_no_sleep(void)
{
    power_inputs_t in = { .now_ms = 100000, .last_engine_running_ms = 99000,
                          .last_elm_ok_ms = 99000, .battery_v = 12.4f };
    TEST_ASSERT_FALSE(power_should_sleep(&in));
}

static void test_idle_low_v_sleeps(void)
{
    power_inputs_t in = { .now_ms = 10 * MIN_MS, .last_engine_running_ms = 4 * MIN_MS,
                          .last_elm_ok_ms = 4 * MIN_MS, .battery_v = 12.4f };
    TEST_ASSERT_TRUE(power_should_sleep(&in));
}

static void test_idle_high_v_no_sleep(void)
{
    power_inputs_t in = { .now_ms = 10 * MIN_MS, .last_engine_running_ms = 4 * MIN_MS,
                          .last_elm_ok_ms = 4 * MIN_MS, .battery_v = 13.8f };
    TEST_ASSERT_FALSE(power_should_sleep(&in));
}

static void test_elm_gone_sleeps(void)
{
    power_inputs_t in = { .now_ms = 10 * MIN_MS, .last_engine_running_ms = 4 * MIN_MS,
                          .last_elm_ok_ms = 4 * MIN_MS, .battery_v = NAN };
    TEST_ASSERT_TRUE(power_should_sleep(&in));
}

static void test_ecu_answering_rpm_zero_sleeps(void)
{
    /* Spec: RPM = 0 OU ELM mudo. A ECU responde (há 1 s), mas o RPM está em 0 há 6 min
       e a bateria está em 12,4 V (ignição ligada, motor parado): dorme. */
    power_inputs_t in = { .now_ms = 10 * MIN_MS, .last_engine_running_ms = 4 * MIN_MS,
                          .last_elm_ok_ms = 10 * MIN_MS - 1000, .battery_v = 12.4f };
    TEST_ASSERT_TRUE(power_should_sleep(&in));
}

static void test_idle_boundary(void)
{
    power_inputs_t in = { .now_ms = POWER_IDLE_MS, .last_engine_running_ms = 0,
                          .last_elm_ok_ms = 0, .battery_v = 12.0f };
    TEST_ASSERT_TRUE(power_should_sleep(&in));
    in.now_ms = POWER_IDLE_MS - 1;
    TEST_ASSERT_FALSE(power_should_sleep(&in));
    in.now_ms = POWER_IDLE_MS;
    in.battery_v = POWER_SLEEP_V;       /* 13.0 V não é < 13.0 V */
    TEST_ASSERT_FALSE(power_should_sleep(&in));
}

static void test_wrap_safe(void)
{
    /* now deu a volta no uint32: ocioso = 6 min. */
    power_inputs_t in = { .now_ms = 5 * MIN_MS, .last_engine_running_ms = UINT32_MAX - MIN_MS + 1,
                          .last_elm_ok_ms = UINT32_MAX - MIN_MS + 1, .battery_v = 12.0f };
    TEST_ASSERT_TRUE(power_should_sleep(&in));
    in.now_ms = 2 * MIN_MS;             /* ocioso = 3 min */
    TEST_ASSERT_FALSE(power_should_sleep(&in));
}

static void test_sleep_after_override(void)
{
    power_inputs_t in = { .now_ms = 20000, .last_engine_running_ms = 0,
                          .last_elm_ok_ms = 0, .battery_v = 12.0f };
    TEST_ASSERT_FALSE(power_should_sleep(&in));
    TEST_ASSERT_TRUE(power_should_sleep_after(&in, 10000));
    TEST_ASSERT_FALSE(power_should_sleep_after(&in, 20001));
    in.now_ms = 10 * MIN_MS;
    TEST_ASSERT_EQUAL(power_should_sleep(&in), power_should_sleep_after(&in, POWER_IDLE_MS));
}

static void test_wake_rules(void)
{
    TEST_ASSERT_TRUE(power_should_stay_awake_after_wake(13.5f, 0.0f));
    TEST_ASSERT_FALSE(power_should_stay_awake_after_wake(12.5f, 0.0f));
    TEST_ASSERT_TRUE(power_should_stay_awake_after_wake(12.5f, 800.0f));
    TEST_ASSERT_FALSE(power_should_stay_awake_after_wake(NAN, 0.0f));
    TEST_ASSERT_FALSE(power_should_stay_awake_after_wake(NAN, NAN));
    TEST_ASSERT_TRUE(power_should_stay_awake_after_wake(NAN, 800.0f));
    TEST_ASSERT_TRUE(power_should_stay_awake_after_wake(POWER_RUN_V, NAN));
}

void run_power_tests(void)
{
    RUN_TEST(test_running_no_sleep);
    RUN_TEST(test_idle_low_v_sleeps);
    RUN_TEST(test_idle_high_v_no_sleep);
    RUN_TEST(test_elm_gone_sleeps);
    RUN_TEST(test_ecu_answering_rpm_zero_sleeps);
    RUN_TEST(test_idle_boundary);
    RUN_TEST(test_wrap_safe);
    RUN_TEST(test_sleep_after_override);
    RUN_TEST(test_wake_rules);
}
