#include <string.h>
#include "storage.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_rom_crc.h"

#define NS            "carmon"
#define BLOB_VERSION  1
#define KEY_MAX       15          /* limite de chave do NVS */
#define BLOB_MAX      (2 + VP_MAX_GEARS * sizeof(float) + 4)

/* Chave "r_<id>"; falha se não couber em 15 chars. */
static esp_err_t ratios_key(const char *id, char key[KEY_MAX + 1])
{
    if (!id || !id[0] || strlen(id) + 2 > KEY_MAX) return ESP_ERR_INVALID_ARG;
    key[0] = 'r'; key[1] = '_';
    strcpy(key + 2, id);
    return ESP_OK;
}

static uint32_t blob_crc(const uint8_t *buf, size_t len)
{
    return esp_rom_crc32_le(0, buf, len);
}

esp_err_t storage_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        e = nvs_flash_erase();
        if (e != ESP_OK) return e;
        e = nvs_flash_init();
    }
    return e;
}

esp_err_t storage_save_ratios(const char *profile_id, const float *ratios, uint8_t n)
{
    char key[KEY_MAX + 1];
    esp_err_t e = ratios_key(profile_id, key);
    if (e != ESP_OK) return e;
    if (!ratios || n < 1 || n > VP_MAX_GEARS) return ESP_ERR_INVALID_ARG;

    /* Serialização explícita: {version, n, float[n], crc32}; CRC sobre os bytes anteriores. */
    uint8_t buf[BLOB_MAX];
    size_t len = 0;
    buf[len++] = BLOB_VERSION;
    buf[len++] = n;
    memcpy(buf + len, ratios, n * sizeof(float));
    len += n * sizeof(float);
    uint32_t crc = blob_crc(buf, len);
    for (int i = 0; i < 4; i++) buf[len++] = (uint8_t)(crc >> (8 * i));

    nvs_handle_t h;
    e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    /* nvs_set_blob grava de forma atômica: ou o blob novo inteiro, ou o antigo permanece. */
    e = nvs_set_blob(h, key, buf, len);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

esp_err_t storage_load_ratios(const char *profile_id, float out[VP_MAX_GEARS], uint8_t *n)
{
    char key[KEY_MAX + 1];
    esp_err_t e = ratios_key(profile_id, key);
    if (e != ESP_OK) return e;
    if (!out || !n) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    e = nvs_open(NS, NVS_READONLY, &h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;  /* namespace ainda não existe */
    if (e != ESP_OK) return e;
    uint8_t buf[BLOB_MAX + 1];
    size_t len = sizeof buf;
    e = nvs_get_blob(h, key, buf, &len);
    nvs_close(h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (e == ESP_ERR_NVS_INVALID_LENGTH) return ESP_ERR_INVALID_CRC;  /* maior que qualquer blob válido */
    if (e != ESP_OK) return e;

    if (len < 2 + sizeof(float) + 4 || buf[0] != BLOB_VERSION) return ESP_ERR_INVALID_CRC;
    uint8_t cnt = buf[1];
    if (cnt < 1 || cnt > VP_MAX_GEARS || len != 2 + cnt * sizeof(float) + 4) return ESP_ERR_INVALID_CRC;
    size_t body = len - 4;
    uint32_t stored = 0;
    for (int i = 0; i < 4; i++) stored |= (uint32_t)buf[body + i] << (8 * i);
    if (stored != blob_crc(buf, body)) return ESP_ERR_INVALID_CRC;

    memset(out, 0, VP_MAX_GEARS * sizeof(float));
    memcpy(out, buf + 2, cnt * sizeof(float));
    *n = cnt;
    return ESP_OK;
}

esp_err_t storage_erase_ratios(const char *profile_id)
{
    char key[KEY_MAX + 1];
    esp_err_t e = ratios_key(profile_id, key);
    if (e != ESP_OK) return e;
    nvs_handle_t h;
    e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_erase_key(h, key);
    if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

bool storage_get_mute(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    if (nvs_get_u8(h, "mute", &v) != ESP_OK) v = 0;
    nvs_close(h);
    return v != 0;
}

esp_err_t storage_set_mute(bool m)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_u8(h, "mute", m ? 1 : 0);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

esp_err_t storage_get_elm_mac(uint8_t mac[6])
{
    if (!mac) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READONLY, &h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (e != ESP_OK) return e;
    uint8_t tmp[6];
    size_t len = sizeof tmp;
    e = nvs_get_blob(h, "elm_mac", tmp, &len);
    nvs_close(h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (e != ESP_OK) return e;
    if (len != 6) return ESP_ERR_INVALID_SIZE;
    memcpy(mac, tmp, 6);
    return ESP_OK;
}

esp_err_t storage_set_elm_mac(const uint8_t mac[6])
{
    if (!mac) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_blob(h, "elm_mac", mac, 6);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

esp_err_t storage_get_profile_id(char *out, size_t cap)
{
    if (!out || cap == 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READONLY, &h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (e != ESP_OK) return e;
    size_t len = cap;
    e = nvs_get_str(h, "profile", out, &len);
    nvs_close(h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (e == ESP_ERR_NVS_INVALID_LENGTH) return ESP_ERR_INVALID_SIZE;
    return e;
}

esp_err_t storage_set_profile_id(const char *id)
{
    if (!id || !id[0]) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_str(h, "profile", id);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}
