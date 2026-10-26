#pragma once
/* API pública do motor de marchas: aprendizado, detecção e conselho de troca.
   Lógica pura (sem FreeRTOS nem hardware); o tempo vem sempre em now_ms. */
#include <stdbool.h>
#include <stdint.h>
#include "vehicle_profile.h"

typedef struct {
    uint32_t now_ms, sample_ms;         /* sample_ms = instante da amostra mais velha entre rpm/speed */
    float rpm, speed_kmh, load_pct;
    float throttle_pct;                  /* NAN se o carro não fornece */
} gear_input_t;

typedef enum { SHIFT_NONE, SHIFT_UP, SHIFT_DOWN } shift_advice_t;

typedef struct {
    uint8_t gear;          /* 0 = "–" */
    shift_advice_t advice;
    uint8_t target_gear;   /* 0 se advice == NONE */
    bool learning;         /* aprendizado ainda não convergiu */
    uint8_t learned_gears;
    bool stale;
} gear_state_t;

/* Estado interno: os tipos de gear_learn/gear_detect são privados ao componente,
   então o armazenamento é opaco aqui (tamanhos conferidos por _Static_assert em gear_engine.c). */
#define GEAR_ENGINE_LEARN_WORDS  192
#define GEAR_ENGINE_DETECT_WORDS 2
#define GEAR_ENGINE_ADVICE_WORDS 4

typedef struct {
    const vehicle_profile_t *profile;
    uint32_t learn_mem[GEAR_ENGINE_LEARN_WORDS];
    uint32_t detect_mem[GEAR_ENGINE_DETECT_WORDS];
    uint32_t advice_mem[GEAR_ENGINE_ADVICE_WORDS];   /* histerese do conselho */
    float    ratios[VP_MAX_GEARS];   /* relações em uso (sementes ou aprendidas) */
    float    last_ratio;             /* relação da amostra anterior (0 = inválida) */
    float    throttle_rest;          /* menor 0x11 visto com rpm > 0, até 25% (NAN = nenhum) */
    uint8_t  n;                      /* nº de relações em uso */
    uint8_t  learner_count;          /* último resultado do aprendiz (cache) */
    uint8_t  since_check;            /* amostras estáveis desde a última checagem */
    bool     converged;
    bool     learned_pending;        /* take_learned ainda não consumido */
} gear_engine_t;

/* learned: NULL ou p->gears valores vindos do NVS (inicia já convergido). */
void         gear_engine_init(gear_engine_t *g, const vehicle_profile_t *p, const float *learned);
gear_state_t gear_engine_update(gear_engine_t *g, const gear_input_t *in);
/* true uma única vez, logo após convergir. */
bool         gear_engine_take_learned(gear_engine_t *g, float out[VP_MAX_GEARS]);
/* Descarta o aprendido e volta às sementes do perfil. */
void         gear_engine_reset_learning(gear_engine_t *g);
