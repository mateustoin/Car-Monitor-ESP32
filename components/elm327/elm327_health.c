#include "elm327.h"

void elm327_health_init(elm327_health_t *h)
{
    h->elm_errors = 0;
}

elm327_link_action_t elm327_health_update(elm327_health_t *h, esp_err_t err, bool link_up)
{
    if (!link_up || err == ESP_ERR_INVALID_STATE || err == ESP_FAIL) {
        h->elm_errors = 0;
        return ELM327_LINK_RECONNECT;
    }
    if (err == ESP_OK || err == ESP_ERR_NOT_FOUND) {
        h->elm_errors = 0;
        return ELM327_LINK_KEEP;
    }
    if (++h->elm_errors < ELM327_REINIT_AFTER) return ELM327_LINK_KEEP;
    h->elm_errors = 0;
    return ELM327_LINK_REINIT;
}
