#include "gear_learn.h"
#include <string.h>
#include <stdbool.h>

/* Histograma com bins logarítmicos de ~2% a partir de RATIO_MIN. */
#define RATIO_MIN       2.0f
#define RATIO_SANE_MAX  200.0f
#define BIN_GROWTH      1.02f
#define PEAK_HALF       2    /* janela de pico: ±2 bins (~±4%) */
#define CLUSTER_HALF    4    /* região do cluster e supressão: ±4 bins (~8%) */
#define MIN_SAMPLES     40
#define MIN_FRACTION_PC 5    /* % do total */
#define MAX_CLUSTERS    16

void gear_learn_init(gear_learn_t *l, uint8_t gears)
{
    memset(l, 0, sizeof(*l));
    l->gears = gears;
}

void gear_learn_add(gear_learn_t *l, float ratio)
{
    if (!(ratio >= RATIO_MIN) || ratio > RATIO_SANE_MAX) return; /* cobre NaN, <=0 */
    /* Sem logf/powf (libm não é linkada em todos os alvos): varre as bordas. */
    int bin = 0;
    float edge = RATIO_MIN * BIN_GROWTH;
    while (ratio >= edge && bin < GEAR_LEARN_BINS) { bin++; edge *= BIN_GROWTH; }
    if (bin >= GEAR_LEARN_BINS) return; /* acima da faixa de interesse */
    l->counts[bin]++;
    l->total++;
}

static float bin_center(int bin)
{
    float lo = RATIO_MIN;
    for (int i = 0; i < bin; i++) lo *= BIN_GROWTH;
    return lo * (1.0f + (BIN_GROWTH - 1.0f) * 0.5f);
}

static uint32_t window_mass(const uint32_t *c, int center, int half)
{
    uint32_t m = 0;
    for (int i = center - half; i <= center + half; i++) {
        if (i >= 0 && i < GEAR_LEARN_BINS) m += c[i];
    }
    return m;
}

uint8_t gear_learn_result(const gear_learn_t *l, float out[VP_MAX_GEARS])
{
    uint32_t work[GEAR_LEARN_BINS];
    float val[MAX_CLUSTERS];
    int n = 0;
    int limit = l->gears < VP_MAX_GEARS ? l->gears : VP_MAX_GEARS;
    if (limit > MAX_CLUSTERS) limit = MAX_CLUSTERS;

    memcpy(work, l->counts, sizeof(work));

    /* Extrai clusters do mais populoso para o menos; clusters a menos de ~8%
       ficam fundidos porque a região suprimida os absorve. */
    while (n < limit) {
        int peak = -1;
        uint32_t best = 0;
        for (int i = 0; i < GEAR_LEARN_BINS; i++) {
            uint32_t m = window_mass(work, i, PEAK_HALF);
            if (m > best) { best = m; peak = i; }
        }
        if (peak < 0) break;

        uint32_t mass = window_mass(work, peak, CLUSTER_HALF);
        if (mass < MIN_SAMPLES || mass * 100u < (uint32_t)MIN_FRACTION_PC * l->total) break;

        float sum = 0.0f;
        for (int i = peak - CLUSTER_HALF; i <= peak + CLUSTER_HALF; i++) {
            if (i < 0 || i >= GEAR_LEARN_BINS) continue;
            sum += (float)work[i] * bin_center(i);
            work[i] = 0;
        }
        val[n++] = sum / (float)mass;
    }

    /* Ordem crescente (inserção): out[0] = 1ª marcha. */
    for (int i = 1; i < n; i++) {
        float v = val[i];
        int j = i - 1;
        while (j >= 0 && val[j] > v) { val[j + 1] = val[j]; j--; }
        val[j + 1] = v;
    }
    for (int i = 0; i < n; i++) out[i] = val[i];
    return (uint8_t)n;
}

bool gear_learn_converged(const gear_learn_t *l)
{
    float out[VP_MAX_GEARS];
    return l->gears > 0 && gear_learn_result(l, out) == l->gears;
}
