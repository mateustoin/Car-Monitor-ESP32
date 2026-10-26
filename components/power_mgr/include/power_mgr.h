#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t now_ms;
    uint32_t last_engine_running_ms;  /* última leitura de RPM > 0: mede a ociosidade */
    uint32_t last_elm_ok_ms;          /* só informativo (log/diagnóstico); não entra na regra */
    float battery_v;               /* NAN = desconhecida */
} power_inputs_t;

#define POWER_IDLE_MS     (5u * 60u * 1000u)
#define POWER_SLEEP_V     13.0f
#define POWER_WAKE_MS     (10u * 60u * 1000u)
#define POWER_RUN_V       13.2f

/* Dorme se now - last_engine_running_ms >= POWER_IDLE_MS (RPM = 0 ou ELM mudo, como no spec)
   e (bateria < POWER_SLEEP_V ou desconhecida). */
bool power_should_sleep(const power_inputs_t *in);

/* Igual a power_should_sleep, mas com limite de ociosidade informado (depuração). */
bool power_should_sleep_after(const power_inputs_t *in, uint32_t idle_ms);

/* Após acordar: continua ligado se v >= POWER_RUN_V ou rpm > 0. */
bool power_should_stay_awake_after_wake(float battery_v, float rpm);
