#include "shift_advisor.h"
#include <math.h>

#define THROTTLE_CLOSED_PCT 2.0f
#define LOAD_CLOSED_PCT     15.0f  /* fallback sem sensor de acelerador */

static float up_limit(const vehicle_profile_t *p, float load)
{
    if (load <= p->load_light_pct) return p->up_rpm_light;
    if (load >= p->load_heavy_pct) return p->up_rpm_heavy;
    float f = (load - p->load_light_pct) / (float)(p->load_heavy_pct - p->load_light_pct);
    return p->up_rpm_light + f * (float)(p->up_rpm_heavy - p->up_rpm_light);
}

static bool throttle_closed(const gear_input_t *in)
{
    if (isnan(in->throttle_pct)) return in->load_pct < LOAD_CLOSED_PCT;
    return in->throttle_pct <= THROTTLE_CLOSED_PCT;
}

shift_advice_t shift_advise(const vehicle_profile_t *p, const float *ratios, uint8_t n,
                            uint8_t gear, const gear_input_t *in, uint8_t *target)
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
        && !throttle_closed(in)) {
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
