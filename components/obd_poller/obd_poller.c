#include "obd_poller.h"
#include "elm327.h"

#define SLOW_COOLANT_MS 5000u
#define SLOW_MS 1000u
#define RATE_ALPHA 0.2f

static bool is_supported(obd_supported_fn fn, void *arg, uint8_t pid)
{
    return fn == NULL || fn(arg, pid);
}

static void add_item(obd_poller_t *p, uint8_t pid, uint32_t period_ms)
{
    if (p->n_items >= OBD_POLLER_MAX_ITEMS) return;
    p->items[p->n_items].pid = pid;
    p->items[p->n_items].period_ms = period_ms;
    p->items[p->n_items].last_ms = 0;
    p->n_items++;
}

void obd_poller_init(obd_poller_t *p, obd_supported_fn supported, void *arg)
{
    *p = (obd_poller_t){0};
    bool has_load = is_supported(supported, arg, ELM_PID_LOAD);
    bool has_thr = is_supported(supported, arg, ELM_PID_THROTTLE);

    /* RPM e velocidade sempre entram: sem elas o produto não serve e algumas ECUs mentem na máscara. */
    add_item(p, ELM_PID_RPM, 0);
    add_item(p, ELM_PID_SPEED, 0);
    if (has_load) add_item(p, ELM_PID_LOAD, 0);
    if (has_thr) add_item(p, ELM_PID_THROTTLE, 0);  /* uma vez só, mesmo sendo fonte de carga */

    if (is_supported(supported, arg, ELM_PID_COOLANT)) add_item(p, ELM_PID_COOLANT, SLOW_COOLANT_MS);
    if (is_supported(supported, arg, ELM_PID_MAP)) add_item(p, ELM_PID_MAP, SLOW_MS);
    if (is_supported(supported, arg, ELM_PID_IAT)) add_item(p, ELM_PID_IAT, SLOW_MS);

    p->snap.load_source_pid = has_load ? ELM_PID_LOAD : (has_thr ? ELM_PID_THROTTLE : 0);
}

uint8_t obd_poller_next(obd_poller_t *p, uint32_t now_ms)
{
    if (p->n_items == 0) return 0;
    if (!p->started) {
        p->started = true;
        for (uint8_t i = 0; i < p->n_items; i++) p->items[i].last_ms = now_ms;
    }

    /* Lento vencido: o mais atrasado vence. */
    int best = -1;
    uint32_t best_over = 0;
    for (uint8_t i = 0; i < p->n_items; i++) {
        obd_poll_item_t *it = &p->items[i];
        if (it->period_ms == 0) continue;
        uint32_t elapsed = now_ms - it->last_ms;
        if (elapsed < it->period_ms) continue;
        uint32_t over = elapsed - it->period_ms;
        if (best < 0 || over > best_over) { best = i; best_over = over; }
    }
    if (best >= 0) {
        p->items[best].last_ms = now_ms;
        return p->items[best].pid;
    }

    /* Rápidos em round-robin. */
    for (uint8_t n = 0; n < p->n_items; n++) {
        uint8_t i = (uint8_t)((p->rr + n) % p->n_items);
        if (p->items[i].period_ms == 0) {
            p->rr = (uint8_t)((i + 1) % p->n_items);
            return p->items[i].pid;
        }
    }
    return 0;
}

static void set_value(obd_value_t *v, float value, uint32_t now_ms)
{
    v->value = value;
    v->t_ms = now_ms;
    v->valid = true;
}

void obd_poller_record(obd_poller_t *p, uint8_t pid, float value, bool ok, uint32_t now_ms)
{
    if (!ok) return;
    obd_snapshot_t *s = &p->snap;
    switch (pid) {
    case ELM_PID_RPM:      set_value(&s->rpm, value, now_ms); break;
    case ELM_PID_SPEED:    set_value(&s->speed_kmh, value, now_ms); break;
    case ELM_PID_LOAD:     set_value(&s->load_pct, value, now_ms); break;
    case ELM_PID_THROTTLE:
        set_value(&s->throttle_pct, value, now_ms);
        if (s->load_source_pid == ELM_PID_THROTTLE) set_value(&s->load_pct, value, now_ms);
        break;
    case ELM_PID_MAP:      set_value(&s->map_kpa, value, now_ms); break;
    case ELM_PID_COOLANT:  set_value(&s->coolant_c, value, now_ms); break;
    case ELM_PID_IAT:      set_value(&s->iat_c, value, now_ms); break;
    default: return;
    }

    if (p->have_ok) {
        uint32_t dt = now_ms - p->last_ok_ms;
        if (dt > 0) {
            float inst = 1000.0f / (float)dt;
            s->rate_hz = (s->rate_hz == 0.0f) ? inst : s->rate_hz + RATE_ALPHA * (inst - s->rate_hz);
        }
    }
    p->have_ok = true;
    p->last_ok_ms = now_ms;
}

const obd_snapshot_t *obd_poller_snapshot(const obd_poller_t *p)
{
    return &p->snap;
}
