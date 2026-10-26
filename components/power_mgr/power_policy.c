#include "power_mgr.h"
#include <math.h>

bool power_should_sleep_after(const power_inputs_t *in, uint32_t idle_ms)
{
    /* Spec: RPM = 0 OU ELM sem resposta. Os dois casos param de atualizar
       last_engine_running_ms (só RPM > 0 atualiza), então ele sozinho mede a ociosidade;
       uma ECU que responde com RPM 0 não mantém o aparelho acordado.
       Aritmética uint32 segura contra volta do contador. */
    uint32_t idle = in->now_ms - in->last_engine_running_ms;
    if (idle < idle_ms) {
        return false;
    }
    return isnan(in->battery_v) || in->battery_v < POWER_SLEEP_V;
}

bool power_should_sleep(const power_inputs_t *in)
{
    return power_should_sleep_after(in, POWER_IDLE_MS);
}

bool power_should_stay_awake_after_wake(float battery_v, float rpm)
{
    return battery_v >= POWER_RUN_V || rpm > 0.0f;   /* NAN compara falso */
}
