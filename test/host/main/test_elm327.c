#include <string.h>
#include "unity.h"
#include "elm327.h"
#include "transport_replay.h"

static float decode1(uint8_t pid, uint8_t a, uint8_t b)
{
    uint8_t d[2] = { a, b };
    float v = -1.0f;
    TEST_ASSERT_EQUAL(ESP_OK, elm327_decode_pid(pid, d, 2, &v));
    return v;
}

static void test_parse_plain(void)
{
    uint8_t d[4]; size_t n = 0; float v;
    TEST_ASSERT_EQUAL(ESP_OK, elm327_parse_mode01("410C1AF8", 0x0C, d, sizeof(d), &n));
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_HEX8(0x1A, d[0]);
    TEST_ASSERT_EQUAL_HEX8(0xF8, d[1]);
    TEST_ASSERT_EQUAL(ESP_OK, elm327_decode_pid(0x0C, d, n, &v));
    TEST_ASSERT_EQUAL_FLOAT(1726.0f, v);
}

static void test_parse_with_searching(void)
{
    uint8_t d[4]; size_t n = 0; float v;
    TEST_ASSERT_EQUAL(ESP_OK, elm327_parse_mode01("SEARCHING...\r410D3C", 0x0D, d, sizeof(d), &n));
    TEST_ASSERT_EQUAL(ESP_OK, elm327_decode_pid(0x0D, d, n, &v));
    TEST_ASSERT_EQUAL_FLOAT(60.0f, v);
}

static void test_parse_bus_init(void)
{
    uint8_t d[4]; size_t n = 0; float v;
    TEST_ASSERT_EQUAL(ESP_OK, elm327_parse_mode01("BUS INIT: ...OK\r410D3C", 0x0D, d, sizeof(d), &n));
    TEST_ASSERT_EQUAL(ESP_OK, elm327_decode_pid(0x0D, d, n, &v));
    TEST_ASSERT_EQUAL_FLOAT(60.0f, v);
}

static void test_parse_no_data(void)
{
    uint8_t d[4]; size_t n = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, elm327_parse_mode01("NO DATA", 0x0C, d, sizeof(d), &n));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, elm327_parse_mode01("?", 0x0C, d, sizeof(d), &n));
}

static void test_parse_elm_errors(void)
{
    /* R32: esquisitices do ELM são ESP_ERR_INVALID_RESPONSE (ESP_FAIL fica só para o transporte). */
    uint8_t d[4]; size_t n = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, elm327_parse_mode01("STOPPED", 0x0C, d, sizeof(d), &n));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, elm327_parse_mode01("CAN ERROR", 0x0C, d, sizeof(d), &n));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, elm327_parse_mode01("BUS BUSY", 0x0C, d, sizeof(d), &n));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, elm327_parse_mode01("UNABLE TO CONNECT", 0x0C, d, sizeof(d), &n));
    /* sem a linha 41 do PID pedido (resposta de outro PID ou vazia) */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, elm327_parse_mode01("410D3C", 0x0C, d, sizeof(d), &n));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, elm327_parse_mode01("", 0x0C, d, sizeof(d), &n));
}

static void test_health_counter(void)
{
    elm327_health_t h;
    elm327_health_init(&h);
    /* respostas normais e NO DATA mantêm o link */
    TEST_ASSERT_EQUAL(ELM327_LINK_KEEP, elm327_health_update(&h, ESP_OK, true));
    TEST_ASSERT_EQUAL(ELM327_LINK_KEEP, elm327_health_update(&h, ESP_ERR_NOT_FOUND, true));
    /* 3 erros de ELM (ou timeouts) seguidos com o link de pé -> reinit do ELM, sem derrubar */
    TEST_ASSERT_EQUAL(ELM327_LINK_KEEP, elm327_health_update(&h, ESP_ERR_INVALID_RESPONSE, true));
    TEST_ASSERT_EQUAL(ELM327_LINK_KEEP, elm327_health_update(&h, ESP_ERR_TIMEOUT, true));
    TEST_ASSERT_EQUAL(ELM327_LINK_REINIT, elm327_health_update(&h, ESP_ERR_INVALID_RESPONSE, true));
    /* o contador recomeça depois do reinit */
    TEST_ASSERT_EQUAL(ELM327_LINK_KEEP, elm327_health_update(&h, ESP_ERR_INVALID_RESPONSE, true));
    TEST_ASSERT_EQUAL(ELM327_LINK_KEEP, elm327_health_update(&h, ESP_ERR_INVALID_RESPONSE, true));
    /* uma resposta boa no meio zera a sequência */
    TEST_ASSERT_EQUAL(ELM327_LINK_KEEP, elm327_health_update(&h, ESP_ERR_NOT_FOUND, true));
    TEST_ASSERT_EQUAL(ELM327_LINK_KEEP, elm327_health_update(&h, ESP_ERR_INVALID_RESPONSE, true));
    TEST_ASSERT_EQUAL(ELM327_LINK_KEEP, elm327_health_update(&h, ESP_ERR_INVALID_RESPONSE, true));
    TEST_ASSERT_EQUAL(ELM327_LINK_REINIT, elm327_health_update(&h, ESP_ERR_TIMEOUT, true));
    /* erros de transporte ou link caído -> reconectar na hora */
    TEST_ASSERT_EQUAL(ELM327_LINK_RECONNECT, elm327_health_update(&h, ESP_ERR_INVALID_STATE, true));
    TEST_ASSERT_EQUAL(ELM327_LINK_RECONNECT, elm327_health_update(&h, ESP_FAIL, true));
    TEST_ASSERT_EQUAL(ELM327_LINK_RECONNECT, elm327_health_update(&h, ESP_ERR_TIMEOUT, false));
    TEST_ASSERT_EQUAL(ELM327_LINK_RECONNECT, elm327_health_update(&h, ESP_OK, false));
    /* depois de um RECONNECT o contador recomeça */
    TEST_ASSERT_EQUAL(ELM327_LINK_KEEP, elm327_health_update(&h, ESP_ERR_INVALID_RESPONSE, true));
    TEST_ASSERT_EQUAL(ELM327_LINK_KEEP, elm327_health_update(&h, ESP_ERR_INVALID_RESPONSE, true));
}

static void test_parse_multi_ecu(void)
{
    uint8_t d[4]; size_t n = 0; float v;
    TEST_ASSERT_EQUAL(ESP_OK, elm327_parse_mode01("7E8\r410C0FA0\r410C0FA0", 0x0C, d, sizeof(d), &n));
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL(ESP_OK, elm327_decode_pid(0x0C, d, n, &v));
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, v);
}

static void test_decode_all(void)
{
    TEST_ASSERT_EQUAL_FLOAT(100.0f, decode1(ELM_PID_THROTTLE, 255, 0));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, decode1(ELM_PID_LOAD, 255, 0));
    TEST_ASSERT_EQUAL_FLOAT(90.0f, decode1(ELM_PID_COOLANT, 130, 0));
    TEST_ASSERT_EQUAL_FLOAT(35.0f, decode1(ELM_PID_MAP, 35, 0));
    TEST_ASSERT_EQUAL_FLOAT(-40.0f, decode1(ELM_PID_IAT, 0, 0));
    float v;
    uint8_t d[1] = { 1 };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, elm327_decode_pid(ELM_PID_RPM, d, 1, &v));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, elm327_decode_pid(0x7F, d, 1, &v));
}

/* Transporte que embrulha outro e conta os comandos enviados. */
typedef struct {
    obd_transport_t base;
    obd_transport_t *inner;
    int n_0120;
    int n_total;
} counting_t;

static esp_err_t c_write(obd_transport_t *t, const char *d, size_t l)
{
    counting_t *c = t->ctx;
    c->n_total++;
    if (l >= 4 && strncmp(d, "0120", 4) == 0) c->n_0120++;
    return c->inner->ops->write(c->inner, d, l);
}
static int c_read(obd_transport_t *t, char *b, size_t cap, uint32_t ms)
{ counting_t *c = t->ctx; return c->inner->ops->read(c->inner, b, cap, ms); }
static bool c_conn(obd_transport_t *t)
{ counting_t *c = t->ctx; return c->inner->ops->is_connected(c->inner); }

static const obd_transport_ops_t counting_ops = {
    .write = c_write, .read = c_read, .is_connected = c_conn };

static const char *INIT_REC =
    "0\tATZ\tELM327 v1.5\n"
    "1\tATE0\tOK\n"
    "2\tATL0\tOK\n"
    "3\tATS0\tOK\n"
    "4\tATH0\tOK\n"
    "5\tATSP0\tOK\n"
    "6\t0100\tSEARCHING...|4100BE3FA812\n"
    "7\tATDPN\tA5\n"
    "8\t010C\t410C1AF8\n"
    "9\tATRV\t12.6V\n";

static void counting_open(counting_t *c, const char *rec)
{
    memset(c, 0, sizeof(*c));
    TEST_ASSERT_EQUAL(ESP_OK, transport_replay_create(rec, 0, &c->inner));
    c->inner->ops->connect(c->inner, 100);
    c->base.ops = &counting_ops;
    c->base.ctx = c;
}

static void test_init_and_supported(void)
{
    counting_t c; elm327_t e;
    counting_open(&c, INIT_REC);
    TEST_ASSERT_EQUAL(ESP_OK, elm327_init(&e, &c.base, 1000));
    TEST_ASSERT_TRUE(elm327_pid_supported(&e, 0x0C));
    TEST_ASSERT_TRUE(elm327_pid_supported(&e, 0x0B));
    TEST_ASSERT_FALSE(elm327_pid_supported(&e, 0x0A));
    TEST_ASSERT_FALSE(elm327_pid_supported(&e, 0x20 + 1));
    TEST_ASSERT_EQUAL_STRING("A5", e.protocol);
    TEST_ASSERT_EQUAL(0, c.n_0120);
    TEST_ASSERT_EQUAL(8, c.n_total);
    c.inner->ops->destroy(c.inner);
}

static void test_init_queries_0120_when_bit_set(void)
{
    counting_t c; elm327_t e;
    counting_open(&c,
        "0\tATZ\tELM327\n1\tATE0\tOK\n2\tATL0\tOK\n3\tATS0\tOK\n4\tATH0\tOK\n5\tATSP0\tOK\n"
        "6\t0100\t4100BE3FA813\n7\tATDPN\tA6\n8\t0120\t412080000001\n9\t0140\t4140FFFFFFFF\n");
    TEST_ASSERT_EQUAL(ESP_OK, elm327_init(&e, &c.base, 1000));
    TEST_ASSERT_EQUAL(1, c.n_0120);
    TEST_ASSERT_TRUE(elm327_pid_supported(&e, 0x21));
    TEST_ASSERT_TRUE(elm327_pid_supported(&e, 0x40));
    TEST_ASSERT_TRUE(elm327_pid_supported(&e, 0x41));
    c.inner->ops->destroy(c.inner);
}

static void test_init_fails_without_0100(void)
{
    counting_t c; elm327_t e;
    counting_open(&c, "0\tATZ\tELM327\n6\t0100\tUNABLE TO CONNECT\n");
    TEST_ASSERT_NOT_EQUAL(ESP_OK, elm327_init(&e, &c.base, 1000));
    c.inner->ops->destroy(c.inner);
}

static void test_read_voltage(void)
{
    counting_t c; elm327_t e; float v = 0;
    counting_open(&c, INIT_REC);
    TEST_ASSERT_EQUAL(ESP_OK, elm327_init(&e, &c.base, 1000));
    TEST_ASSERT_EQUAL(ESP_OK, elm327_read_voltage(&e, &v));
    TEST_ASSERT_EQUAL_FLOAT(12.6f, v);
    c.inner->ops->destroy(c.inner);
}

static char tap_cmd[16], tap_resp[64];
static int tap_n;
static void tap_cb(void *arg, const char *cmd, const char *resp)
{
    (void)arg;
    strncpy(tap_cmd, cmd, sizeof(tap_cmd) - 1);
    strncpy(tap_resp, resp, sizeof(tap_resp) - 1);
    tap_n++;
}

static void test_tap_called(void)
{
    counting_t c; elm327_t e; float v = 0;
    counting_open(&c, INIT_REC);
    TEST_ASSERT_EQUAL(ESP_OK, elm327_init(&e, &c.base, 1000));
    tap_n = 0;
    elm327_set_tap(&e, tap_cb, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, elm327_read_pid(&e, ELM_PID_RPM, &v));
    TEST_ASSERT_EQUAL_FLOAT(1726.0f, v);
    TEST_ASSERT_EQUAL(1, tap_n);
    TEST_ASSERT_EQUAL_STRING("010C", tap_cmd);
    TEST_ASSERT_EQUAL_STRING("410C1AF8", tap_resp);
    c.inner->ops->destroy(c.inner);
}

/* Com init_with_tap a troca de inicialização também passa pelo tap (gravação reproduzível). */
static void test_init_with_tap_sees_init(void)
{
    counting_t c; elm327_t e;
    counting_open(&c, INIT_REC);
    tap_n = 0;
    tap_cmd[0] = '\0';
    memset(&e, 0xA5, sizeof(e));   /* lixo: init_with_tap não pode depender do conteúdo anterior */
    TEST_ASSERT_EQUAL(ESP_OK, elm327_init_with_tap(&e, &c.base, 1000, tap_cb, NULL));
    TEST_ASSERT_EQUAL(8, tap_n);
    TEST_ASSERT_EQUAL_STRING("ATDPN", tap_cmd);
    TEST_ASSERT_EQUAL_STRING("A5", tap_resp);
    TEST_ASSERT_TRUE(e.tap == tap_cb);
    c.inner->ops->destroy(c.inner);
}

static void test_read_voltage_without_number(void)
{
    /* clone sem ATRV: "?" com o link de pé é erro do ELM, não do transporte */
    counting_t c; elm327_t e; float v = 0;
    counting_open(&c, "0\tATZ\tELM327\n1\tATE0\tOK\n2\tATL0\tOK\n3\tATS0\tOK\n4\tATH0\tOK\n"
                      "5\tATSP0\tOK\n6\t0100\t4100BE3FA812\n7\tATDPN\tA5\n8\tATRV\t?\n");
    TEST_ASSERT_EQUAL(ESP_OK, elm327_init(&e, &c.base, 1000));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, elm327_read_voltage(&e, &v));
    c.inner->ops->destroy(c.inner);
}

static void test_read_pid_bus_busy(void)
{
    counting_t c; elm327_t e; float v = 0;
    counting_open(&c, "0\tATZ\tELM327\n1\tATE0\tOK\n2\tATL0\tOK\n3\tATS0\tOK\n4\tATH0\tOK\n"
                      "5\tATSP0\tOK\n6\t0100\t4100BE3FA812\n7\tATDPN\tA5\n8\t010C\tBUS BUSY\n");
    TEST_ASSERT_EQUAL(ESP_OK, elm327_init(&e, &c.base, 1000));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, elm327_read_pid(&e, ELM_PID_RPM, &v));
    c.inner->ops->destroy(c.inner);
}

static void test_read_pid_no_data(void)
{
    counting_t c; elm327_t e; float v = 0;
    counting_open(&c, INIT_REC);
    TEST_ASSERT_EQUAL(ESP_OK, elm327_init(&e, &c.base, 1000));
    /* 010D não está na gravação: o replay responde NO DATA */
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, elm327_read_pid(&e, ELM_PID_SPEED, &v));
    c.inner->ops->destroy(c.inner);
}

void run_elm327_tests(void)
{
    RUN_TEST(test_parse_plain);
    RUN_TEST(test_parse_with_searching);
    RUN_TEST(test_parse_bus_init);
    RUN_TEST(test_parse_no_data);
    RUN_TEST(test_parse_elm_errors);
    RUN_TEST(test_health_counter);
    RUN_TEST(test_read_voltage_without_number);
    RUN_TEST(test_read_pid_bus_busy);
    RUN_TEST(test_parse_multi_ecu);
    RUN_TEST(test_decode_all);
    RUN_TEST(test_init_and_supported);
    RUN_TEST(test_init_queries_0120_when_bit_set);
    RUN_TEST(test_init_fails_without_0100);
    RUN_TEST(test_read_voltage);
    RUN_TEST(test_tap_called);
    RUN_TEST(test_init_with_tap_sees_init);
    RUN_TEST(test_read_pid_no_data);
}
