#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "transport_replay.h"
#include "obd_recorder.h"

#define REPLAY_LINE_MAX 640
#define REPLAY_CMD_MAX  64
#define REPLAY_RESP_MAX 512

typedef struct {
    obd_transport_t base;
    char *rec;             /* cópia da gravação */
    size_t cursor;         /* offset da próxima linha a examinar */
    size_t chunk;
    char pending[REPLAY_RESP_MAX + 8];
    size_t pend_len, pend_pos;
    uint32_t now_ms;
    bool connected;
} replay_t;

static esp_err_t r_connect(obd_transport_t *t, uint32_t timeout_ms)
{
    (void)timeout_ms;
    ((replay_t *)t->ctx)->connected = true;
    return ESP_OK;
}

static void deliver(replay_t *r, const char *resp)
{
    int n = snprintf(r->pending, sizeof(r->pending), "%s\r\r>", resp);
    r->pend_len = (n < 0) ? 0 : ((size_t)n < sizeof(r->pending) ? (size_t)n : sizeof(r->pending) - 1);
    r->pend_pos = 0;
}

static esp_err_t r_write(obd_transport_t *t, const char *data, size_t len)
{
    replay_t *r = t->ctx;
    if (!r->connected) return ESP_ERR_INVALID_STATE;
    char cmd[REPLAY_CMD_MAX];
    while (len > 0 && (data[len - 1] == '\r' || data[len - 1] == '\n')) len--;
    if (len >= sizeof(cmd)) return ESP_ERR_INVALID_SIZE;
    memcpy(cmd, data, len);
    cmd[len] = '\0';

    size_t pos = r->cursor;
    while (r->rec[pos]) {
        const char *eol = strchr(r->rec + pos, '\n');
        size_t ll = eol ? (size_t)(eol - (r->rec + pos)) : strlen(r->rec + pos);
        size_t next = pos + ll + (eol ? 1 : 0);
        if (ll > 0 && ll < REPLAY_LINE_MAX) {
            char line[REPLAY_LINE_MAX], rcmd[REPLAY_CMD_MAX], resp[REPLAY_RESP_MAX];
            uint32_t ts;
            memcpy(line, r->rec + pos, ll);
            line[ll] = '\0';
            if (rec_parse_line(line, &ts, rcmd, sizeof(rcmd), resp, sizeof(resp)) &&
                strcmp(rcmd, cmd) == 0) {
                r->cursor = next;
                r->now_ms = ts;
                deliver(r, resp);
                return ESP_OK;
            }
        }
        pos = next;
    }
    deliver(r, "NO DATA");
    return ESP_OK;
}

static int r_read(obd_transport_t *t, char *buf, size_t cap, uint32_t timeout_ms)
{
    (void)timeout_ms;
    replay_t *r = t->ctx;
    size_t avail = r->pend_len - r->pend_pos;
    if (avail == 0) return 0;
    size_t n = avail;
    if (r->chunk > 0 && n > r->chunk) n = r->chunk;
    if (n > cap) n = cap;
    memcpy(buf, r->pending + r->pend_pos, n);
    r->pend_pos += n;
    return (int)n;
}

static void r_disconnect(obd_transport_t *t) { ((replay_t *)t->ctx)->connected = false; }
static bool r_is_connected(obd_transport_t *t) { return ((replay_t *)t->ctx)->connected; }

static void r_destroy(obd_transport_t *t)
{
    replay_t *r = t->ctx;
    free(r->rec);
    free(r);
}

static const obd_transport_ops_t replay_ops = {
    .connect = r_connect, .write = r_write, .read = r_read,
    .disconnect = r_disconnect, .is_connected = r_is_connected, .destroy = r_destroy,
};

esp_err_t transport_replay_create(const char *recording, size_t chunk, obd_transport_t **out)
{
    if (!recording || !out) return ESP_ERR_INVALID_ARG;
    replay_t *r = calloc(1, sizeof(*r));
    if (!r) return ESP_ERR_NO_MEM;
    r->rec = strdup(recording);
    if (!r->rec) { free(r); return ESP_ERR_NO_MEM; }
    r->chunk = chunk;
    r->base.ops = &replay_ops;
    r->base.ctx = r;
    *out = &r->base;
    return ESP_OK;
}

uint32_t transport_replay_now_ms(obd_transport_t *t)
{
    return ((replay_t *)t->ctx)->now_ms;
}
