#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "obd_transport.h"

#define ELM_PID_RPM      0x0C
#define ELM_PID_SPEED    0x0D
#define ELM_PID_THROTTLE 0x11
#define ELM_PID_LOAD     0x04
#define ELM_PID_MAP      0x0B
#define ELM_PID_COOLANT  0x05
#define ELM_PID_IAT      0x0F

typedef void (*elm327_tap_t)(void *arg, const char *cmd, const char *resp);

typedef struct {
    obd_transport_t *t;
    uint32_t timeout_ms;
    char protocol[32];       /* resposta de ATDPN, ex. "A6" ou "A5" */
    uint32_t supported[3];   /* bitmasks de 0100, 0120 e 0140 */
    elm327_tap_t tap;
    void *tap_arg;
} elm327_t;

/* Inicializa o ELM327 (ATZ..ATSP0, 0100, ATDPN e 0120/0140 se anunciados). Zera *e (tap inclusive). */
esp_err_t elm327_init(elm327_t *e, obd_transport_t *t, uint32_t timeout_ms);
/* Igual a elm327_init, mas já com o tap ligado: a troca de inicialização também passa por ele
   (a gravação fica reproduzível pelo transport_replay). */
esp_err_t elm327_init_with_tap(elm327_t *e, obd_transport_t *t, uint32_t timeout_ms,
                               elm327_tap_t tap, void *tap_arg);
bool      elm327_pid_supported(const elm327_t *e, uint8_t pid);
/* ESP_ERR_NOT_FOUND = NO DATA */
esp_err_t elm327_read_pid(elm327_t *e, uint8_t pid, float *value);
/* ATRV, ex. "12.6V" */
esp_err_t elm327_read_voltage(elm327_t *e, float *volts);
/* Chamado a cada troca comando/resposta bem-sucedida. */
void      elm327_set_tap(elm327_t *e, elm327_tap_t tap, void *arg);

/* Puros (sem transporte).
   Procura a primeira linha que comece com "41"+pid e devolve os bytes de dados.
   ESP_ERR_NOT_FOUND: NO DATA / "?"; ESP_ERR_INVALID_RESPONSE: esquisitice do ELM (STOPPED,
   CAN ERROR, BUS BUSY, UNABLE TO CONNECT...) ou resposta sem a linha 41 do PID;
   ESP_ERR_INVALID_SIZE: dados não cabem em cap. ESP_FAIL nunca sai daqui: nos comandos acima
   ele vem só do transporte (queda do link no meio do comando). */
esp_err_t elm327_parse_mode01(const char *resp, uint8_t pid, uint8_t *data, size_t cap, size_t *len);
/* ESP_ERR_INVALID_SIZE: bytes insuficientes; ESP_ERR_NOT_SUPPORTED: PID desconhecido. */
esp_err_t elm327_decode_pid(uint8_t pid, const uint8_t *data, size_t len, float *value);

/* Saúde da sessão (puro, R32): decide, a cada resultado de comando, o que fazer com o link.
   - link caído, ESP_ERR_INVALID_STATE ou ESP_FAIL (transporte): reconectar;
   - ESP_OK ou NO DATA: o ELM respondeu, zera a sequência de erros;
   - qualquer outro erro (ELM esquisito, timeout com o link de pé): conta; ao chegar a
     ELM327_REINIT_AFTER seguidos, refazer o elm327_init sem derrubar o link. */
#define ELM327_REINIT_AFTER 3

typedef enum { ELM327_LINK_KEEP, ELM327_LINK_REINIT, ELM327_LINK_RECONNECT } elm327_link_action_t;
typedef struct { uint8_t elm_errors; } elm327_health_t;

void                 elm327_health_init(elm327_health_t *h);
elm327_link_action_t elm327_health_update(elm327_health_t *h, esp_err_t err, bool link_up);
