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
