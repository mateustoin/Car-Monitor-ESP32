#include <stdlib.h>
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void run_transport_tests(void);
void run_elm327_tests(void);
void run_vehicle_profile_tests(void);
void run_gear_learn_tests(void);
void run_gear_detect_tests(void);
void run_shift_advisor_tests(void);
void run_gear_engine_tests(void);
void run_obd_poller_tests(void);
void run_storage_tests(void);
void run_ui_logic_tests(void);
void run_power_tests(void);
void run_rec_files_tests(void);
void run_rate_model_tests(void);

static void test_sanity(void)
{
    TEST_ASSERT_EQUAL(1, 1);
}

/* Cada test_<x>.c expõe run_<x>_tests(); declare e chame aqui.
   No alvo linux o ponto de entrada é app_main (simulador FreeRTOS). */

void app_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sanity);
    run_transport_tests();
    run_elm327_tests();
    run_vehicle_profile_tests();
    run_gear_learn_tests();
    run_gear_detect_tests();
    run_shift_advisor_tests();
    run_gear_engine_tests();
    run_obd_poller_tests();
    run_storage_tests();
    run_ui_logic_tests();
    run_power_tests();
    run_rec_files_tests();
    run_rate_model_tests();
    exit(UNITY_END());
}
