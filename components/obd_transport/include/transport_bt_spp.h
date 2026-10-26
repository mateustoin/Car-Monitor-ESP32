#pragma once
#include <stdint.h>
#include "obd_transport.h"

/* Transporte Bluetooth clássico (SPP) para adaptadores ELM327.
   Só existe com CONFIG_OBD_TRANSPORT_BT_SPP (ESP32 com BT clássico habilitado). O app não
   inclui este header: usa obd_transport_create_default() e obd_transport_get_peer().

   - O NVS precisa estar inicializado antes (storage_init / nvs_flash_init): o
     Bluedroid guarda as chaves de pareamento nele.
   - Instância única: os callbacks do Bluedroid são globais. Um segundo create sem
     destroy do primeiro devolve ESP_ERR_INVALID_STATE.
   - O controlador e o Bluedroid são iniciados no primeiro create e continuam de pé
     depois do destroy; um create posterior reaproveita a pilha.
   - connect(): com mac conecta direto (descoberta SPP no endereço); sem mac faz a
     busca GAP e casa o nome pelo EIR ou pelo nome remoto que o Bluedroid lê no fim
     da busca. A busca dura (timeout - 6 s) / 1,28 s voltas, de 1 a 8 (~1,3 a ~10 s);
     os 6 s restantes ficam para o nome remoto, o SDP, o page e o pareamento.
     Timeout mínimo útil ~8 s; recomendado 16 s (busca completa).
   - Depois de achar/conectar o adaptador, os connect() seguintes vão direto ao MAC
     lembrado; após 3 falhas diretas seguidas, voltam a fazer a busca.
   - PIN legado fixo (cfg->pin); se o adaptador usar SSP, a confirmação é aceita.
   - Depois de uma queda (ESP_SPP_CLOSE_EVT), write devolve ESP_ERR_INVALID_STATE e
     read devolve < 0 até um novo connect(). */
typedef struct {
    const char *name;        /* CONFIG_OBD_ELM_BT_NAME */
    const char *pin;         /* CONFIG_OBD_ELM_BT_PIN */
    const uint8_t *mac;      /* NULL = procurar por nome */
} transport_bt_spp_config_t;

esp_err_t transport_bt_spp_create(const transport_bt_spp_config_t *cfg, obd_transport_t **out);

/* MAC do aparelho conectado (ou do último conectado). ESP_ERR_INVALID_STATE se
   nunca conectou. */
esp_err_t transport_bt_spp_get_peer(obd_transport_t *t, uint8_t mac[6]);
