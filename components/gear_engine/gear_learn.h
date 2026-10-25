#pragma once
/* Aprendizado automático das relações de marcha (km/h por 1000 RPM).
   Histograma de memória fixa com bins logarítmicos; sem malloc. */
#include <stdbool.h>
#include <stdint.h>
#include "vehicle_profile.h"

#define GEAR_LEARN_BINS 190

typedef struct {
    uint32_t counts[GEAR_LEARN_BINS];
    uint32_t total;   /* amostras válidas aceitas */
    uint8_t  gears;   /* nº de marchas esperado */
} gear_learn_t;

void    gear_learn_init(gear_learn_t *l, uint8_t gears);
/* Só amostras já filtradas como estáveis. Valores <= 0, NaN, < 2 ou acima de ~86 (fim dos bins) são ignorados. */
void    gear_learn_add(gear_learn_t *l, float ratio);
/* Devolve o nº de clusters confiáveis (limitado a `gears`), gravados em out[]
   em ordem crescente de relação (out[0] = 1ª marcha). Se houver mais clusters
   que `gears`, ficam os mais populosos. */
uint8_t gear_learn_result(const gear_learn_t *l, float out[VP_MAX_GEARS]);
/* result == gears */
bool    gear_learn_converged(const gear_learn_t *l);
