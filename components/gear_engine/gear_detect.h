#pragma once
/* Detecção da marcha com histerese temporal (sem FreeRTOS nem hardware). */
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t  shown;               /* marcha exibida: 1..n, 0 = desconhecida */
    uint8_t  candidate;           /* candidato atual */
    uint32_t candidate_since_ms;  /* desde quando o candidato se mantém */
} gear_detect_t;

void gear_detect_init(gear_detect_t *d);
/* Devolve a marcha exibida (1..n) ou 0 = desconhecida ("–"). ratios[0..n-1]
   crescentes (km/h por 1000 RPM); n = nº de marchas conhecidas. Com rpm < 500
   ou speed < 3 o candidato é 0. tolerance é relativa a cada relação. `shown`
   só muda (inclusive para 0) após o mesmo candidato persistir hold_ms. */
uint8_t gear_detect_update(gear_detect_t *d, const float *ratios, uint8_t n,
                           float rpm, float speed_kmh, float tolerance, uint16_t hold_ms, uint32_t now_ms);
/* true quando o candidato atual é a marcha exibida (nenhuma troca em andamento). */
bool    gear_detect_settled(const gear_detect_t *d);
