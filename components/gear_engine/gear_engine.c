#include "gear_engine.h"
#include <math.h>
#include <string.h>
#include "gear_learn.h"
#include "gear_detect.h"
#include "shift_advisor.h"

_Static_assert(sizeof(gear_learn_t) <= sizeof(((gear_engine_t *)0)->learn_mem), "learn_mem pequeno");
_Static_assert(sizeof(gear_detect_t) <= sizeof(((gear_engine_t *)0)->detect_mem), "detect_mem pequeno");
_Static_assert(_Alignof(gear_learn_t) <= _Alignof(uint32_t), "alinhamento learn");
_Static_assert(_Alignof(gear_detect_t) <= _Alignof(uint32_t), "alinhamento detect");

/* Filtro de amostra estável para o aprendizado */
#define STABLE_MIN_SPEED_KMH 10.0f
#define STABLE_MIN_RPM       1000.0f
#define STABLE_MIN_LOAD_PCT  10.0f
#define STABLE_MIN_THROTTLE  3.0f
#define STABLE_MAX_RATIO_JUMP 0.03f
/* gear_learn_converged refaz o clustering a cada chamada: só checa a cada N amostras estáveis */
#define CHECK_EVERY 20

#define LEARN(g)  ((gear_learn_t *)(g)->learn_mem)
#define DETECT(g) ((gear_detect_t *)(g)->detect_mem)

static void use_seeds(gear_engine_t *g)
{
    g->n = 0;
    memset(g->ratios, 0, sizeof(g->ratios));
    for (int i = 0; i < VP_MAX_GEARS; i++) {
        if (g->profile->seed_ratios[i] != 0) g->ratios[g->n++] = g->profile->seed_ratios[i];
    }
}

static void clear_learner(gear_engine_t *g)
{
    gear_learn_init(LEARN(g), g->profile->gears);
    g->learner_count = 0;
    g->since_check = 0;
    g->last_ratio = 0;
}

/* Blob vindo do NVS não é confiável: exatamente p->gears valores finitos, > 0 e estritamente crescentes. */
static bool learned_valid(const vehicle_profile_t *p, const float *learned)
{
    if (p->gears == 0 || p->gears > VP_MAX_GEARS) return false;
    for (int i = 0; i < p->gears; i++) {
        if (!isfinite(learned[i]) || !(learned[i] > 0)) return false;
        if (i > 0 && !(learned[i] > learned[i - 1])) return false;
    }
    return true;
}

void gear_engine_init(gear_engine_t *g, const vehicle_profile_t *p, const float *learned)
{
    memset(g, 0, sizeof(*g));
    g->profile = p;
    gear_detect_init(DETECT(g));
    clear_learner(g);
    if (learned && learned_valid(p, learned)) {
        g->n = p->gears > VP_MAX_GEARS ? VP_MAX_GEARS : p->gears;
        memcpy(g->ratios, learned, g->n * sizeof(float));
        g->converged = true;
    } else {
        use_seeds(g);
    }
}

static bool is_stable(const gear_input_t *in, float ratio, float prev_ratio)
{
    if (in->speed_kmh < STABLE_MIN_SPEED_KMH || in->rpm < STABLE_MIN_RPM) return false;
    if (!(in->load_pct >= STABLE_MIN_LOAD_PCT || in->throttle_pct > STABLE_MIN_THROTTLE)) return false;
    if (prev_ratio <= 0) return false;
    return fabsf(ratio - prev_ratio) < STABLE_MAX_RATIO_JUMP * prev_ratio;
}

static void learn_step(gear_engine_t *g, const gear_input_t *in)
{
    float ratio = (in->rpm > 0 && in->speed_kmh > 0) ? in->speed_kmh / (in->rpm / 1000.0f) : 0;
    bool stable = is_stable(in, ratio, g->last_ratio);
    g->last_ratio = ratio;
    if (!stable || g->converged) return;

    gear_learn_add(LEARN(g), ratio);
    if (++g->since_check < CHECK_EVERY) return;
    g->since_check = 0;
    float out[VP_MAX_GEARS];
    g->learner_count = gear_learn_result(LEARN(g), out);
    /* R29: só converge com relações plausíveis (crescentes, passo >= 8%). */
    if (g->learner_count == g->profile->gears && g->learner_count > 0
        && gear_learn_plausible(out, g->learner_count)) {
        memset(g->ratios, 0, sizeof(g->ratios));
        memcpy(g->ratios, out, g->learner_count * sizeof(float));
        g->n = g->learner_count;
        g->converged = true;
        g->learned_pending = true;
    }
}

gear_state_t gear_engine_update(gear_engine_t *g, const gear_input_t *in)
{
    const vehicle_profile_t *p = g->profile;
    gear_state_t s = {0};
    s.stale = (uint32_t)(in->now_ms - in->sample_ms) > p->stale_ms;
    if (s.stale) g->last_ratio = 0;
    else learn_step(g, in);

    float rpm = s.stale ? 0 : in->rpm, spd = s.stale ? 0 : in->speed_kmh;
    uint8_t gear = gear_detect_update(DETECT(g), g->ratios, g->n, rpm, spd,
                                      p->ratio_tolerance, p->gear_hold_ms, in->now_ms);
    s.learning = !g->converged;
    s.learned_gears = (s.learning && g->learner_count > g->n) ? g->learner_count : g->n;
    if (s.stale) return s; /* gear 0, NONE */

    s.gear = gear;
    s.advice = shift_advise(p, g->ratios, g->n, gear, in, &s.target_gear);
    return s;
}

bool gear_engine_take_learned(gear_engine_t *g, float out[VP_MAX_GEARS])
{
    if (!g->learned_pending) return false;
    g->learned_pending = false;
    memset(out, 0, VP_MAX_GEARS * sizeof(float));
    memcpy(out, g->ratios, g->n * sizeof(float));
    return true;
}

void gear_engine_reset_learning(gear_engine_t *g)
{
    clear_learner(g);
    gear_detect_init(DETECT(g));
    g->converged = false;
    g->learned_pending = false;
    use_seeds(g);
}
