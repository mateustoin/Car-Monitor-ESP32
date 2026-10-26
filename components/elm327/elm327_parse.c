#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "elm327.h"

#define LINE_MAX_LEN 64

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Copia a linha [s, s+n) sem espaços para out. false se não couber. */
static bool squeeze(const char *s, size_t n, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == ' ' || s[i] == '\t') continue;
        if (o + 1 >= cap) return false;
        out[o++] = s[i];
    }
    out[o] = '\0';
    return true;
}

esp_err_t elm327_parse_mode01(const char *resp, uint8_t pid, uint8_t *data, size_t cap, size_t *len)
{
    if (!resp || !data || !len) return ESP_ERR_INVALID_ARG;
    char prefix[5];
    snprintf(prefix, sizeof(prefix), "41%02X", pid);
    bool saw_no_data = false;

    const char *p = resp;
    while (*p) {
        size_t n = strcspn(p, "\r\n");
        char line[LINE_MAX_LEN];
        if (n > 0 && squeeze(p, n, line, sizeof(line))) {
            if (strncasecmp(line, prefix, 4) == 0) {
                const char *h = line + 4;
                size_t hl = strlen(h);
                bool ok = (hl % 2 == 0);
                for (size_t i = 0; ok && i < hl; i++) ok = hex_val(h[i]) >= 0;
                if (ok) {
                    if (hl / 2 > cap) return ESP_ERR_INVALID_SIZE;
                    for (size_t i = 0; i < hl / 2; i++)
                        data[i] = (uint8_t)(hex_val(h[2 * i]) * 16 + hex_val(h[2 * i + 1]));
                    *len = hl / 2;
                    return ESP_OK;
                }
            } else if (strcmp(line, "NODATA") == 0 || strcmp(line, "?") == 0) {
                saw_no_data = true;
            }
        }
        p += n;
        p += strspn(p, "\r\n");
    }
    /* STOPPED, CAN ERROR, BUS BUSY, UNABLE TO CONNECT, linha de outro PID...: o ELM
       respondeu, mas sem dado útil (R32: distinto de ESP_FAIL, que é do transporte). */
    return saw_no_data ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t elm327_decode_pid(uint8_t pid, const uint8_t *data, size_t len, float *value)
{
    if (!data || !value) return ESP_ERR_INVALID_ARG;
    size_t need;
    switch (pid) {
    case ELM_PID_RPM: need = 2; break;
    case ELM_PID_SPEED: case ELM_PID_THROTTLE: case ELM_PID_LOAD:
    case ELM_PID_MAP: case ELM_PID_COOLANT: case ELM_PID_IAT: need = 1; break;
    default: return ESP_ERR_NOT_SUPPORTED;
    }
    if (len < need) return ESP_ERR_INVALID_SIZE;
    switch (pid) {
    case ELM_PID_RPM: *value = (float)(256 * data[0] + data[1]) / 4.0f; break;
    case ELM_PID_THROTTLE: case ELM_PID_LOAD: *value = (float)data[0] * 100.0f / 255.0f; break;
    case ELM_PID_COOLANT: case ELM_PID_IAT: *value = (float)data[0] - 40.0f; break;
    default: *value = (float)data[0]; break; /* velocidade (km/h) e MAP (kPa) */
    }
    return ESP_OK;
}
