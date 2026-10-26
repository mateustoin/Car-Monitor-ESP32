#include "unity.h"
#include "obd_recorder.h"

static void test_trip_index_valid(void)
{
    TEST_ASSERT_EQUAL(0, rec_trip_index("trip_000.tsv"));
    TEST_ASSERT_EQUAL(7, rec_trip_index("trip_007.tsv"));
    TEST_ASSERT_EQUAL(123, rec_trip_index("trip_123.tsv"));
    TEST_ASSERT_EQUAL(1234, rec_trip_index("trip_1234.tsv"));
}

static void test_trip_index_invalid(void)
{
    TEST_ASSERT_EQUAL(-1, rec_trip_index(NULL));
    TEST_ASSERT_EQUAL(-1, rec_trip_index(""));
    TEST_ASSERT_EQUAL(-1, rec_trip_index("trip_.tsv"));
    TEST_ASSERT_EQUAL(-1, rec_trip_index("trip_001.txt"));
    TEST_ASSERT_EQUAL(-1, rec_trip_index("trip_001.tsv.bak"));
    TEST_ASSERT_EQUAL(-1, rec_trip_index("trip_1a.tsv"));
    TEST_ASSERT_EQUAL(-1, rec_trip_index("other_001.tsv"));
    TEST_ASSERT_EQUAL(-1, rec_trip_index("trip_1234567.tsv"));
}

void run_rec_files_tests(void)
{
    RUN_TEST(test_trip_index_valid);
    RUN_TEST(test_trip_index_invalid);
}
