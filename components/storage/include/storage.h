#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "vehicle_profile.h"

/* Persistência NVS (namespace "carmon"): relações aprendidas por perfil, mudo, MAC do ELM327
   e perfil ativo. */

/* nvs_flash_init; apaga e reinicia em NO_FREE_PAGES/NEW_VERSION_FOUND. Idempotente. */
esp_err_t storage_init(void);

/* Blob "r_<profile_id>". ESP_ERR_NOT_FOUND se não existe; ESP_ERR_INVALID_CRC se versão/CRC/n inválidos
   (o chamador trata como "sem dados"); ESP_ERR_INVALID_ARG se o id deixaria a chave NVS acima de 15 chars. */
esp_err_t storage_load_ratios(const char *profile_id, float out[VP_MAX_GEARS], uint8_t *n);
esp_err_t storage_save_ratios(const char *profile_id, const float *ratios, uint8_t n);
esp_err_t storage_erase_ratios(const char *profile_id);

bool      storage_get_mute(void);   /* false se nunca gravado */
esp_err_t storage_set_mute(bool m);

esp_err_t storage_get_elm_mac(uint8_t mac[6]);   /* ESP_ERR_NOT_FOUND se nunca pareado */
esp_err_t storage_set_elm_mac(const uint8_t mac[6]);
esp_err_t storage_erase_elm_mac(void);           /* ESP_OK também se já não havia MAC */

esp_err_t storage_get_profile_id(char *out, size_t cap);  /* ESP_ERR_NOT_FOUND se vazio */
esp_err_t storage_set_profile_id(const char *id);
