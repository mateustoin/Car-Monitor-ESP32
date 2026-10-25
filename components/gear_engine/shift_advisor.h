#pragma once
/* Conselheiro econômico de troca de marcha (lógica pura). */
#include <stdint.h>
#include "gear_engine.h"

/* ratios[0..n-1] crescentes; gear 1..n (0 = sem conselho). target recebe a marcha
   alvo, ou 0 se o conselho for SHIFT_NONE. */
shift_advice_t shift_advise(const vehicle_profile_t *p, const float *ratios, uint8_t n,
                            uint8_t gear, const gear_input_t *in, uint8_t *target);
