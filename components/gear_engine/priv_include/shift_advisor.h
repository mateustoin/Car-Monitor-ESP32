#pragma once
/* Conselheiro econômico de troca de marcha (lógica pura). */
#include <stdint.h>
#include "gear_engine.h"

/* Acelerador "fechado": throttle <= throttle_rest_pct + 2 (repouso aprendido, R30); sem
   sensor (NAN), carga < 15%. */
bool shift_throttle_closed(const gear_input_t *in, float throttle_rest_pct);

/* Conselho instantâneo (sem histerese). ratios[0..n-1] crescentes; gear 1..n (0 = sem
   conselho). throttle_rest_pct = leitura de 0x11 com o pé fora (0 se desconhecida).
   target recebe a marcha alvo, ou 0 se o conselho for SHIFT_NONE. */
shift_advice_t shift_advise(const vehicle_profile_t *p, const float *ratios, uint8_t n,
                            uint8_t gear, const gear_input_t *in, float throttle_rest_pct,
                            uint8_t *target);

/* Conselho com histerese (R33), o que vai para a tela:
   - novo conselho só aparece depois de o conselho instantâneo persistir SHIFT_DWELL_MS;
   - UP só some com rpm < limite de subida - SHIFT_UP_EXIT_RPM; DOWN só com
     rpm > lug_rpm + SHIFT_DOWN_EXIT_RPM ou com o acelerador fechado (injeção cortada);
   - NONE (e estado zerado) enquanto o detector não está assentado, sem marcha ou ao
     trocar de marcha. */
#define SHIFT_DWELL_MS       400u
#define SHIFT_UP_EXIT_RPM    150.0f
#define SHIFT_DOWN_EXIT_RPM  150.0f

typedef struct {
    shift_advice_t shown;      /* conselho exibido */
    shift_advice_t pending;    /* conselho instantâneo aguardando o dwell */
    uint32_t pending_since_ms;
    uint8_t  gear;             /* marcha a que shown/pending se referem */
    uint8_t  target;
} shift_hyst_t;

void           shift_hyst_reset(shift_hyst_t *h);
shift_advice_t shift_hyst_update(shift_hyst_t *h, const vehicle_profile_t *p, const float *ratios,
                                 uint8_t n, uint8_t gear, bool settled, const gear_input_t *in,
                                 float throttle_rest_pct, uint8_t *target);
