#pragma once
/* Task OBD: conecta no ELM327, inicializa, faz o polling dos PIDs e publica o status. */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_timer.h"
#include "obd_poller.h"

typedef enum {
    APP_OBD_NO_TRANSPORT,   /* build sem transporte (ex.: esp32c3 sem BT clássico) */
    APP_OBD_CONNECTING,
    APP_OBD_NO_ELM,         /* última tentativa falhou; esperando o backoff */
    APP_OBD_CONNECTED,      /* link de pé e ELM inicializado */
} app_obd_state_t;

typedef struct {
    app_obd_state_t state;
    bool connected;                /* state == APP_OBD_CONNECTED */
    obd_snapshot_t snap;           /* zerado a cada nova sessão */
    float battery_v;               /* ATRV; NAN se desconhecida ou desconectado */
    char protocol[32];             /* ATDPN da sessão atual ("" se nenhuma) */
    uint8_t n_supported;           /* nº de PIDs suportados (sem os marcadores 0x20/0x40/0x60) */
    uint32_t last_elm_ok_ms;       /* última leitura de PID com dado válido */
    uint32_t last_engine_running_ms; /* última leitura de RPM > 0 */
} app_obd_status_t;

/* Relógio comum às tasks (ms desde o boot). */
static inline uint32_t app_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Cria a task (prioridade 5, 6 KB). NVS (storage_init) precisa estar pronto. */
esp_err_t app_obd_start(void);
/* Cópia do status atual, sob mutex. */
void      app_obd_get(app_obd_status_t *out);
/* Esquece o MAC salvo e reconecta procurando pelo nome (botão B longo). */
void      app_obd_request_repair(void);
