#pragma once
#include "obd_transport.h"

/* Transporte que reproduz uma gravação (formato de obd_recorder.h).
   chunk > 0 entrega a resposta em pedaços desse tamanho (simula SPP picado);
   chunk == 0 entrega tudo de uma vez. A gravação é copiada. */
esp_err_t transport_replay_create(const char *recording, size_t chunk, obd_transport_t **out);

/* Timestamp (ms) do último registro entregue. */
uint32_t transport_replay_now_ms(obd_transport_t *t);

/* true quando o cursor passou do último registro (nada mais a entregar). */
bool transport_replay_eof(obd_transport_t *t);
