#include "gear_learn.h"
#include <string.h>
#include <stdbool.h>

/* Histograma com bins logarítmicos de ~2% a partir de RATIO_MIN. */
#define RATIO_MIN       2.0f
#define BIN_GROWTH      1.02f
#define PEAK_HALF       1    /* janela de busca do pico: ±1 bin */
#define REGION_HALF     2    /* pico e região do cluster: ±2 bins (~±4%) */
#define MERGE_RATIO     1.08f /* centros a menos de 8% são fundidos */
#define CAND_MIN_SAMPLES 5   /* candidatos pequenos só viram ruído de cauda */
#define MIN_SAMPLES     40
#define MIN_FRACTION_PC 5    /* % do MAIOR cluster (R29: a 1ª é rara na direção real) */
#define MIN_STEP_RATIO  1.08f /* relações vizinhas plausíveis: >= 8% de distância */
#define MAX_CLUSTERS    16

void gear_learn_init(gear_learn_t *l, uint8_t gears)
{
    memset(l, 0, sizeof(*l));
    l->gears = gears;
}

void gear_learn_add(gear_learn_t *l, float ratio)
{
    if (!(ratio >= RATIO_MIN)) return; /* cobre NaN, <=0; o topo é limitado pelos bins */
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

/* Funde, em ordem, centros a menos de 8% um do outro (média ponderada). */
static int merge_close(float *val, uint32_t *mass, int n)
{
    bool again = true;
    while (again) {
        again = false;
        for (int i = 0; i < n && !again; i++) {
            for (int j = i + 1; j < n && !again; j++) {
                float lo = val[i] < val[j] ? val[i] : val[j];
                float hi = val[i] < val[j] ? val[j] : val[i];
                if (hi < lo * MERGE_RATIO) {
                    uint32_t m = mass[i] + mass[j];
                    val[i] = (val[i] * (float)mass[i] + val[j] * (float)mass[j]) / (float)m;
                    mass[i] = m;
                    val[j] = val[n - 1];
                    mass[j] = mass[n - 1];
                    n--;
                    again = true;
                }
            }
        }
    }
    return n;
}

uint8_t gear_learn_result(const gear_learn_t *l, float out[VP_MAX_GEARS])
{
    uint32_t work[GEAR_LEARN_BINS];
    float val[MAX_CLUSTERS];
    uint32_t mass[MAX_CLUSTERS];
    int n = 0;
    int limit = l->gears < VP_MAX_GEARS ? l->gears : VP_MAX_GEARS;

    memcpy(work, l->counts, sizeof(work));

    /* Extrai regiões estreitas (pico +-2 bins, ~+-4%) do mais populoso para o
       menos; marchas a ~10% de distância não se contaminam. Caudas de clusters
       largos viram candidatos próximos e são refundidos depois. */
    while (n < MAX_CLUSTERS) {
        int peak = -1;
        uint32_t best = 0;
        for (int i = 0; i < GEAR_LEARN_BINS; i++) {
            uint32_t m = window_mass(work, i, PEAK_HALF);
            if (m > best) { best = m; peak = i; }
        }
        if (peak < 0) break;

        /* Mean-shift: recentra a região na média local, para não ficar
           entre dois clusters vizinhos. */
        for (int it = 0; it < 4; it++) {
            float ws = 0.0f, wm = 0.0f;
            for (int i = peak - REGION_HALF; i <= peak + REGION_HALF; i++) {
                if (i < 0 || i >= GEAR_LEARN_BINS) continue;
                ws += (float)work[i] * (float)i;
                wm += (float)work[i];
            }
            if (wm <= 0.0f) break;
            int c = (int)(ws / wm + 0.5f);
            if (c == peak) break;
            peak = c;
        }
        best = window_mass(work, peak, REGION_HALF);
        if (best < CAND_MIN_SAMPLES) break;

        float sum = 0.0f;
        for (int i = peak - REGION_HALF; i <= peak + REGION_HALF; i++) {
            if (i < 0 || i >= GEAR_LEARN_BINS) continue;
            sum += (float)work[i] * bin_center(i);
            work[i] = 0;
        }
        val[n] = sum / (float)best;
        mass[n] = best;
        n++;
    }

    n = merge_close(val, mass, n);

    /* Confiável: >= 40 amostras e >= 5% do maior cluster (após a fusão). Relativo ao maior,
       e não ao total, para a 1ª (pouco usada) não ficar de fora com as outras bem povoadas. */
    uint32_t largest = 0;
    for (int i = 0; i < n; i++) if (mass[i] > largest) largest = mass[i];
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (mass[i] >= MIN_SAMPLES && (uint64_t)mass[i] * 100u >= (uint64_t)MIN_FRACTION_PC * largest) {
            val[k] = val[i];
            mass[k] = mass[i];
            k++;
        }
    }
    n = k;

    /* Mantém os `limit` mais populosos. */
    while (n > limit) {
        int w = 0;
        for (int i = 1; i < n; i++) if (mass[i] < mass[w]) w = i;
        val[w] = val[n - 1];
        mass[w] = mass[n - 1];
        n--;
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

bool gear_learn_plausible(const float *ratios, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++) {
        if (!(ratios[i] > 0.0f)) return false;   /* também rejeita NaN */
        if (i > 0 && !(ratios[i] >= ratios[i - 1] * MIN_STEP_RATIO)) return false;
    }
    return true;
}

bool gear_learn_converged(const gear_learn_t *l)
{
    float out[VP_MAX_GEARS];
    return l->gears > 0 && gear_learn_result(l, out) == l->gears && gear_learn_plausible(out, l->gears);
}
