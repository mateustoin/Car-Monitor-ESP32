#include "power_mgr.h"
#include <math.h>

bool power_should_sleep_after(const power_inputs_t *in, uint32_t idle_ms)
{
    /* Aritmética uint32 segura contra volta do contador. */
    uint32_t since_engine = in->now_ms - in->last_engine_running_ms;
    uint32_t since_elm = in->now_ms - in->last_elm_ok_ms;
    uint32_t idle = since_engine < since_elm ? since_engine : since_elm;
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
