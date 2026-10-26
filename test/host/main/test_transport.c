#include <string.h>
#include "unity.h"
#include "obd_transport.h"
#include "transport_replay.h"
#include "obd_recorder.h"

static void test_rec_roundtrip(void)
{
    char line[128], cmd[16], resp[64];
    uint32_t t = 0;
    int n = rec_format_line(line, sizeof(line), 1234, "010C", "SEARCHING...\r410C1AF8");
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING("1234\t010C\tSEARCHING...|410C1AF8\n", line);
    TEST_ASSERT_TRUE(rec_parse_line(line, &t, cmd, sizeof(cmd), resp, sizeof(resp)));
    TEST_ASSERT_EQUAL_UINT32(1234, t);
    TEST_ASSERT_EQUAL_STRING("010C", cmd);
    TEST_ASSERT_EQUAL_STRING("SEARCHING...\r410C1AF8", resp);
    TEST_ASSERT_EQUAL(-1, rec_format_line(line, 10, 1, "010C", "41"));
    TEST_ASSERT_FALSE(rec_parse_line("garbage", &t, cmd, sizeof(cmd), resp, sizeof(resp)));
}

static void test_command_reassembles_chunks(void)
{
    obd_transport_t *t;
    char resp[32];
    TEST_ASSERT_EQUAL(ESP_OK, transport_replay_create("0\t010C\t410C1AF8\n", 3, &t));
    TEST_ASSERT_EQUAL(ESP_OK, t->ops->connect(t, 100));
    TEST_ASSERT_EQUAL(ESP_OK, obd_transport_command(t, "010C", resp, sizeof(resp), 1000));
    TEST_ASSERT_EQUAL_STRING("410C1AF8", resp);
    t->ops->destroy(t);
}

static int never_read(obd_transport_t *t, char *b, size_t c, uint32_t ms)
{ (void)t; (void)b; (void)c; (void)ms; return 0; }
static esp_err_t fake_write(obd_transport_t *t, const char *d, size_t l)
{ (void)t; (void)d; (void)l; return ESP_OK; }
static bool fake_conn(obd_transport_t *t) { (void)t; return true; }

static void test_command_timeout(void)
{
    static const obd_transport_ops_t ops = {
        .write = fake_write, .read = never_read, .is_connected = fake_conn };
    obd_transport_t t = { .ops = &ops, .ctx = NULL };
    char resp[32];
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, obd_transport_command(&t, "010C", resp, sizeof(resp), 350));
}

static void test_command_overflow(void)
{
    obd_transport_t *t;
    char resp[4];
    TEST_ASSERT_EQUAL(ESP_OK, transport_replay_create("0\t010C\t410C1AF8\n", 3, &t));
    t->ops->connect(t, 100);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, obd_transport_command(t, "010C", resp, sizeof(resp), 1000));
    t->ops->destroy(t);
}

static void test_command_disconnected(void)
{
    obd_transport_t *t;
    char resp[16];
    TEST_ASSERT_EQUAL(ESP_OK, transport_replay_create("", 0, &t));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, obd_transport_command(t, "010C", resp, sizeof(resp), 100));
    t->ops->destroy(t);
}

static void test_replay_unknown_cmd(void)
{
    obd_transport_t *t;
    char resp[32];
    TEST_ASSERT_EQUAL(ESP_OK, transport_replay_create("0\t010C\t410C1AF8\n", 0, &t));
    t->ops->connect(t, 100);
    TEST_ASSERT_EQUAL(ESP_OK, obd_transport_command(t, "0105", resp, sizeof(resp), 1000));
    TEST_ASSERT_EQUAL_STRING("NO DATA", resp);
    t->ops->destroy(t);
}

static void test_replay_clock(void)
{
    obd_transport_t *t;
    char resp[32];
    TEST_ASSERT_EQUAL(ESP_OK, transport_replay_create(
        "100\t010C\t410C0000\n500\t010C\t410C1AF8\n", 0, &t));
    t->ops->connect(t, 100);
    obd_transport_command(t, "010C", resp, sizeof(resp), 1000);
    TEST_ASSERT_EQUAL_UINT32(100, transport_replay_now_ms(t));
    obd_transport_command(t, "010C", resp, sizeof(resp), 1000);
    TEST_ASSERT_EQUAL_STRING("410C1AF8", resp);
    TEST_ASSERT_EQUAL_UINT32(500, transport_replay_now_ms(t));
    t->ops->destroy(t);
}

static void test_replay_eof(void)
{
    obd_transport_t *t;
    char resp[32];
    TEST_ASSERT_EQUAL(ESP_OK, transport_replay_create(
        "100\t010C\t410C0000\n500\t010D\t410D10\n", 0, &t));
    t->ops->connect(t, 100);
    TEST_ASSERT_FALSE(transport_replay_eof(t));
    obd_transport_command(t, "010C", resp, sizeof(resp), 1000);
    TEST_ASSERT_FALSE(transport_replay_eof(t));
    obd_transport_command(t, "010D", resp, sizeof(resp), 1000);
    TEST_ASSERT_TRUE(transport_replay_eof(t));
    t->ops->destroy(t);
    TEST_ASSERT_EQUAL(ESP_OK, transport_replay_create("", 0, &t));
    TEST_ASSERT_TRUE(transport_replay_eof(t));
    t->ops->destroy(t);
}

void run_transport_tests(void)
{
    RUN_TEST(test_rec_roundtrip);
    RUN_TEST(test_command_reassembles_chunks);
    RUN_TEST(test_command_timeout);
    RUN_TEST(test_command_overflow);
    RUN_TEST(test_command_disconnected);
    RUN_TEST(test_replay_unknown_cmd);
    RUN_TEST(test_replay_clock);
    RUN_TEST(test_replay_eof);
}
