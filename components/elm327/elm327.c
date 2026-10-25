#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elm327.h"

#define RESP_CAP 128
#define SEARCH_TIMEOUT_MS 10000 /* 0100 dispara a busca de protocolo */

static esp_err_t exchange(elm327_t *e, const char *cmd, char *resp, size_t cap, uint32_t timeout_ms)
{
    esp_err_t err = obd_transport_command(e->t, cmd, resp, cap, timeout_ms);
    if (err != ESP_OK) return err; /* resp pode não estar terminado em erro */
    if (e->tap) e->tap(e->tap_arg, cmd, resp);
    return ESP_OK;
}

static esp_err_t read_support(elm327_t *e, uint8_t base, size_t idx, uint32_t timeout_ms)
{
    char cmd[8], resp[RESP_CAP];
    uint8_t d[8]; size_t n = 0;
    snprintf(cmd, sizeof(cmd), "01%02X", base);
    esp_err_t err = exchange(e, cmd, resp, sizeof(resp), timeout_ms);
    if (err != ESP_OK) return err;
    err = elm327_parse_mode01(resp, base, d, sizeof(d), &n);
    if (err != ESP_OK) return err;
    if (n < 4) return ESP_FAIL;
    e->supported[idx] = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3];
    return ESP_OK;
}

esp_err_t elm327_init(elm327_t *e, obd_transport_t *t, uint32_t timeout_ms)
{
    if (!e || !t) return ESP_ERR_INVALID_ARG;
    memset(e, 0, sizeof(*e));
    e->t = t;
    e->timeout_ms = timeout_ms;

    static const char *const setup[] = { "ATZ", "ATE0", "ATL0", "ATS0", "ATH0", "ATSP0" };
    char resp[RESP_CAP];
    for (size_t i = 0; i < sizeof(setup) / sizeof(setup[0]); i++) {
        esp_err_t err = exchange(e, setup[i], resp, sizeof(resp), timeout_ms);
        if (err != ESP_OK) return err;
    }

    uint32_t long_ms = timeout_ms > SEARCH_TIMEOUT_MS ? timeout_ms : SEARCH_TIMEOUT_MS;
    esp_err_t err = read_support(e, 0x00, 0, long_ms);
    if (err != ESP_OK) return err;

    err = exchange(e, "ATDPN", resp, sizeof(resp), timeout_ms);
    if (err != ESP_OK) return err;
    strncpy(e->protocol, resp, sizeof(e->protocol) - 1);

    /* 0120/0140 só se anunciados; falha de leitura deixa a máscara em zero. */
    if (elm327_pid_supported(e, 0x20)) read_support(e, 0x20, 1, timeout_ms);
    if (elm327_pid_supported(e, 0x40)) read_support(e, 0x40, 2, timeout_ms);
    return ESP_OK;
}

bool elm327_pid_supported(const elm327_t *e, uint8_t pid)
{
    if (!e || pid == 0) return false;
    size_t idx = (size_t)(pid - 1) / 32;
    if (idx >= 3) return false;
    return (e->supported[idx] >> (31 - (pid - 1) % 32)) & 1u;
}

esp_err_t elm327_read_pid(elm327_t *e, uint8_t pid, float *value)
{
    if (!e || !value) return ESP_ERR_INVALID_ARG;
    char cmd[8], resp[RESP_CAP];
    uint8_t d[8]; size_t n = 0;
    snprintf(cmd, sizeof(cmd), "01%02X", pid);
    esp_err_t err = exchange(e, cmd, resp, sizeof(resp), e->timeout_ms);
    if (err != ESP_OK) return err;
    err = elm327_parse_mode01(resp, pid, d, sizeof(d), &n);
    if (err != ESP_OK) return err;
    return elm327_decode_pid(pid, d, n, value);
}

esp_err_t elm327_read_voltage(elm327_t *e, float *volts)
{
    if (!e || !volts) return ESP_ERR_INVALID_ARG;
    char resp[RESP_CAP];
    esp_err_t err = exchange(e, "ATRV", resp, sizeof(resp), e->timeout_ms);
    if (err != ESP_OK) return err;
    char *end;
    float v = strtof(resp, &end);
    if (end == resp) return ESP_FAIL;
    *volts = v;
    return ESP_OK;
}

void elm327_set_tap(elm327_t *e, elm327_tap_t tap, void *arg)
{
    if (!e) return;
    e->tap = tap;
    e->tap_arg = arg;
}
