#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct obd_transport obd_transport_t;

#define OBD_TRANSPORT_PEER_LEN 6   /* endereço do adaptador (MAC BT/BLE) */

typedef struct {
    esp_err_t (*connect)(obd_transport_t *t, uint32_t timeout_ms);
    esp_err_t (*write)(obd_transport_t *t, const char *data, size_t len);
    /* >0 bytes lidos, 0 timeout, <0 erro */
    int  (*read)(obd_transport_t *t, char *buf, size_t cap, uint32_t timeout_ms);
    void (*disconnect)(obd_transport_t *t);
    bool (*is_connected)(obd_transport_t *t);
    void (*destroy)(obd_transport_t *t);
    /* Opcional (NULL se o transporte não tem endereço): endereço do adaptador conectado,
       para o app lembrar e reconectar direto. */
    esp_err_t (*get_peer)(obd_transport_t *t, uint8_t peer[OBD_TRANSPORT_PEER_LEN]);
} obd_transport_ops_t;

struct obd_transport {
    const obd_transport_ops_t *ops;
    void *ctx;
};

/* Envia cmd + "\r" e acumula até '>'. resp sai sem o '>' e sem espaços/CR nas pontas.
   ESP_ERR_TIMEOUT sem '>' no prazo; ESP_ERR_INVALID_SIZE se estourar cap;
   ESP_ERR_INVALID_STATE se desconectado. O prazo é contado pelas fatias de
   timeout passadas a read(), não por relógio de parede. */
esp_err_t obd_transport_command(obd_transport_t *t, const char *cmd, char *resp,
                                size_t cap, uint32_t timeout_ms);

/* Endereço do adaptador. ESP_ERR_NOT_SUPPORTED se o transporte não tem get_peer. */
esp_err_t obd_transport_get_peer(obd_transport_t *t, uint8_t peer[OBD_TRANSPORT_PEER_LEN]);

/* Configuração genérica do transporte padrão do build. */
typedef struct {
    const char *name;      /* nome do adaptador para a busca */
    const char *pin;       /* PIN de pareamento ("" = sem PIN) */
    const uint8_t *peer;   /* endereço salvo (OBD_TRANSPORT_PEER_LEN bytes); NULL = procurar pelo nome */
} obd_transport_default_cfg_t;

/* Cria o transporte escolhido no Kconfig (hoje: Bluetooth clássico SPP, se habilitado).
   ESP_ERR_NOT_SUPPORTED se o build não tem nenhum transporte (ex.: esp32c3, alvo linux);
   *out fica NULL em qualquer erro. */
esp_err_t obd_transport_create_default(const obd_transport_default_cfg_t *cfg, obd_transport_t **out);
