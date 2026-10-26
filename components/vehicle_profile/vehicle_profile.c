#include <math.h>
#include <string.h>
#include "vehicle_profile.h"

extern const vehicle_profile_t profile_sandero_k7m;
extern const vehicle_profile_t profile_generic_manual;

static const vehicle_profile_t *const s_profiles[] = {
    &profile_sandero_k7m,
    &profile_generic_manual,
};

size_t vp_count(void)
{
    return sizeof(s_profiles) / sizeof(s_profiles[0]);
}

const vehicle_profile_t *vp_get(size_t i)
{
    return i < vp_count() ? s_profiles[i] : NULL;
}

const vehicle_profile_t *vp_find(const char *id)
{
    if (id == NULL) return NULL;
    for (size_t i = 0; i < vp_count(); i++) {
        if (strcmp(s_profiles[i]->id, id) == 0) return s_profiles[i];
    }
    return NULL;
}

const vehicle_profile_t *vp_default(void)
{
    return &profile_sandero_k7m;
}

bool vp_validate(const vehicle_profile_t *p)
{
    if (p == NULL || p->id == NULL || p->name == NULL) return false;
    size_t idlen = strlen(p->id);
    if (idlen < 1 || idlen > 12) return false;
    if (p->gears < 1 || p->gears > VP_MAX_GEARS) return false;
    if (p->idle_rpm >= p->redline_rpm) return false;
    if (p->up_rpm_light >= p->up_rpm_heavy) return false;
    if (p->target_band_low >= p->target_band_high) return false;
    if (p->target_band_high > p->redline_rpm) return false;
    if (p->load_light_pct >= p->load_heavy_pct || p->load_heavy_pct > 100) return false;
    if (!(p->ratio_tolerance > 0.0f && p->ratio_tolerance < 0.5f)) return false;

    /* Sementes: finitas, > 0, estritamente crescentes e contíguas a partir da 1ª (0 só no fim:
       um buraco deslocaria a numeração das marchas no gear_engine). */
    float prev = 0.0f;
    bool ended = false;
    for (uint8_t g = 0; g < p->gears; g++) {
        float r = p->seed_ratios[g];
        if (!isfinite(r) || r < 0.0f) return false;
        if (r == 0.0f) { ended = true; continue; }
        if (ended || r <= prev) return false;
        prev = r;
    }
    return true;
}
