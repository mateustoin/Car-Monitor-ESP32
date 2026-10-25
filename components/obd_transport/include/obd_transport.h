#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct obd_transport obd_transport_t;

typedef struct {
    esp_err_t (*connect)(obd_transport_t *t, uint32_t timeout_ms);
    esp_err_t (*write)(obd_transport_t *t, const char *data, size_t len);
    /* >0 bytes lidos, 0 timeout, <0 erro */
    int  (*read)(obd_transport_t *t, char *buf, size_t cap, uint32_t timeout_ms);
    void (*disconnect)(obd_transport_t *t);
    bool (*is_connected)(obd_transport_t *t);
    void (*destroy)(obd_transport_t *t);
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
