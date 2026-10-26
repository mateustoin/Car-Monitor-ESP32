#include "shift_advisor.h"
#include <math.h>
#include <stdbool.h>

#define THROTTLE_CLOSED_PCT 2.0f   /* acima do repouso aprendido */
#define LOAD_CLOSED_PCT     15.0f  /* fallback sem sensor de acelerador */

static float up_limit(const vehicle_profile_t *p, float load)
{
    if (load <= p->load_light_pct) return p->up_rpm_light;
    if (load >= p->load_heavy_pct) return p->up_rpm_heavy;
    float f = (load - p->load_light_pct) / (float)(p->load_heavy_pct - p->load_light_pct);
    return p->up_rpm_light + f * (float)(p->up_rpm_heavy - p->up_rpm_light);
}

bool shift_throttle_closed(const gear_input_t *in, float throttle_rest_pct)
{
    if (isnan(in->throttle_pct)) return in->load_pct < LOAD_CLOSED_PCT;
    return in->throttle_pct <= throttle_rest_pct + THROTTLE_CLOSED_PCT;
}

shift_advice_t shift_advise(const vehicle_profile_t *p, const float *ratios, uint8_t n,
                            uint8_t gear, const gear_input_t *in, float throttle_rest_pct,
                            uint8_t *target)
{
    *target = 0;
    if (gear == 0 || gear > n || !(in->rpm > 0)) return SHIFT_NONE;

    if (gear < n && in->rpm > up_limit(p, in->load_pct)) {
        float after = in->rpm * ratios[gear - 1] / ratios[gear];
        if (after >= (float)p->min_rpm_after_upshift) {
            *target = gear + 1;
            return SHIFT_UP;
        }
    }

    if (gear > 1 && in->rpm < (float)p->lug_rpm && in->load_pct >= (float)p->lug_load_pct
        && !shift_throttle_closed(in, throttle_rest_pct)) {
        uint8_t best = 0;
        for (int k = gear - 1; k >= 1; k--) { /* da maior para a menor: a primeira que serve */
            float pred = in->rpm * ratios[gear - 1] / ratios[k - 1];
            if (pred >= (float)p->target_band_low && pred <= (float)p->redline_rpm) {
                best = (uint8_t)k;
                break;
            }
        }
        if (best == 0) { /* nenhuma na faixa: gear-1, salvo se estourar a rotação máxima */
            best = gear - 1;
            if (in->rpm * ratios[gear - 1] / ratios[best - 1] > (float)p->redline_rpm) return SHIFT_NONE;
        }
        *target = best;
        return SHIFT_DOWN;
    }
    return SHIFT_NONE;
}

void shift_hyst_reset(shift_hyst_t *h)
{
    h->shown = SHIFT_NONE;
    h->pending = SHIFT_NONE;
    h->pending_since_ms = 0;
    h->gear = 0;
    h->target = 0;
}

/* O conselho exibido continua valendo? (faixa de saída) */
static bool still_holds(const shift_hyst_t *h, const vehicle_profile_t *p, const gear_input_t *in,
                        float throttle_rest_pct)
{
    if (h->shown == SHIFT_UP) return in->rpm >= up_limit(p, in->load_pct) - SHIFT_UP_EXIT_RPM;
    if (h->shown == SHIFT_DOWN) {
        return in->rpm <= (float)p->lug_rpm + SHIFT_DOWN_EXIT_RPM
               && !shift_throttle_closed(in, throttle_rest_pct);
    }
    return false;
}

shift_advice_t shift_hyst_update(shift_hyst_t *h, const vehicle_profile_t *p, const float *ratios,
                                 uint8_t n, uint8_t gear, bool settled, const gear_input_t *in,
                                 float throttle_rest_pct, uint8_t *target)
{
    *target = 0;
    if (!settled || gear == 0 || gear != h->gear) {
        shift_hyst_reset(h);
        h->gear = settled ? gear : 0;
        if (!settled || gear == 0) return SHIFT_NONE;
    }

    uint8_t raw_target = 0;
    shift_advice_t raw = shift_advise(p, ratios, n, gear, in, throttle_rest_pct, &raw_target);

    if (h->shown != SHIFT_NONE) {
        if (raw == h->shown) {
            h->target = raw_target;          /* acompanha o alvo (redução pode pular mais) */
        } else if (!still_holds(h, p, in, throttle_rest_pct)) {
            h->shown = SHIFT_NONE;
            h->target = 0;
        }
    }

    if (h->shown == SHIFT_NONE) {
        if (raw != h->pending) {
            h->pending = raw;
            h->pending_since_ms = in->now_ms;
        }
        if (raw != SHIFT_NONE && (uint32_t)(in->now_ms - h->pending_since_ms) >= SHIFT_DWELL_MS) {
            h->shown = raw;
            h->target = raw_target;
            h->pending = SHIFT_NONE;
        }
    } else {
        h->pending = SHIFT_NONE;
    }

    *target = h->shown == SHIFT_NONE ? 0 : h->target;
    return h->shown;
}
