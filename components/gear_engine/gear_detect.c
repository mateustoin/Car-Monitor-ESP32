#include "gear_detect.h"
#include <math.h>

#define MIN_RPM   500.0f
#define MIN_SPEED 3.0f

void gear_detect_init(gear_detect_t *d)
{
    d->shown = 0;
    d->candidate = 0;
    d->candidate_since_ms = 0;
}

static uint8_t pick_candidate(const float *ratios, uint8_t n, float rpm, float speed_kmh, float tolerance)
{
    if (!(rpm >= MIN_RPM) || !(speed_kmh >= MIN_SPEED)) {  /* também rejeita NaN */
        return 0;
    }
    float r = speed_kmh / (rpm / 1000.0f);
    uint8_t best = 0;
    float best_err = 0.0f;
    for (uint8_t i = 0; i < n; i++) {
        if (!(ratios[i] > 0.0f)) {
            continue;
        }
        float err = fabsf(r - ratios[i]) / ratios[i];
        if (err <= tolerance && (best == 0 || err < best_err)) {
            best = (uint8_t)(i + 1);
            best_err = err;
        }
    }
    return best;
}

uint8_t gear_detect_update(gear_detect_t *d, const float *ratios, uint8_t n,
                           float rpm, float speed_kmh, float tolerance, uint16_t hold_ms, uint32_t now_ms)
{
    uint8_t cand = pick_candidate(ratios, n, rpm, speed_kmh, tolerance);
    if (cand != d->candidate) {
        d->candidate = cand;
        d->candidate_since_ms = now_ms;
    }
    if (d->candidate != d->shown && (uint32_t)(now_ms - d->candidate_since_ms) >= hold_ms) {
        d->shown = d->candidate;
    }
    return d->shown;
}
