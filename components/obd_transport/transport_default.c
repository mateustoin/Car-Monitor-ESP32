/* Fábrica do transporte padrão: único ponto que conhece as implementações concretas.
   A escolha segue o Kconfig; o app só vê obd_transport.h. */
#include "obd_transport.h"
#include "sdkconfig.h"
#if CONFIG_OBD_TRANSPORT_BT_SPP
#include "transport_bt_spp.h"
#endif

esp_err_t obd_transport_create_default(const obd_transport_default_cfg_t *cfg, obd_transport_t **out)
{
    if (!cfg || !out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
#if CONFIG_OBD_TRANSPORT_BT_SPP
    transport_bt_spp_config_t bt = { .name = cfg->name, .pin = cfg->pin, .mac = cfg->peer };
    return transport_bt_spp_create(&bt, out);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t obd_transport_get_peer(obd_transport_t *t, uint8_t peer[OBD_TRANSPORT_PEER_LEN])
{
    if (!t || !t->ops || !peer) return ESP_ERR_INVALID_ARG;
    if (!t->ops->get_peer) return ESP_ERR_NOT_SUPPORTED;
    return t->ops->get_peer(t, peer);
}
