#include <string.h>
#include "obd_transport.h"

#define SLICE_MS 100u

static bool is_pad(char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

esp_err_t obd_transport_command(obd_transport_t *t, const char *cmd, char *resp,
                                size_t cap, uint32_t timeout_ms)
{
    if (!t || !t->ops || !cmd || !resp || cap == 0) return ESP_ERR_INVALID_ARG;
    if (!t->ops->is_connected(t)) return ESP_ERR_INVALID_STATE;

    size_t clen = strlen(cmd);
    char line[64];
    if (clen + 1 > sizeof(line)) return ESP_ERR_INVALID_SIZE;
    memcpy(line, cmd, clen);
    line[clen] = '\r';
    esp_err_t err = t->ops->write(t, line, clen + 1);
    if (err != ESP_OK) return err;

    size_t len = 0;
    uint32_t elapsed = 0;
    bool done = false;
    while (!done) {
        if (elapsed >= timeout_ms) return ESP_ERR_TIMEOUT;
        uint32_t slice = timeout_ms - elapsed;
        if (slice > SLICE_MS) slice = SLICE_MS;
        char buf[32];
        int n = t->ops->read(t, buf, sizeof(buf), slice);
        if (n < 0) return ESP_FAIL;
        if (n == 0) { elapsed += slice; continue; }
        for (int i = 0; i < n; i++) {
            if (buf[i] == '>') { done = true; break; }
            if (len + 1 >= cap) return ESP_ERR_INVALID_SIZE;
            resp[len++] = buf[i];
        }
    }
    resp[len] = '\0';

    size_t start = 0;
    while (start < len && is_pad(resp[start])) start++;
    while (len > start && is_pad(resp[len - 1])) len--;
    memmove(resp, resp + start, len - start);
    resp[len - start] = '\0';
    return ESP_OK;
}
