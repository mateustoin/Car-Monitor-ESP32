#pragma once
#include <stdbool.h>
#include <stdint.h>

#define OBD_POLLER_MAX_ITEMS 8

typedef struct { float value; uint32_t t_ms; bool valid; } obd_value_t;

typedef struct {
    obd_value_t rpm, speed_kmh, load_pct, throttle_pct, map_kpa, coolant_c, iat_c;
    float rate_hz;            /* respostas OK por segundo (média móvel) */
    uint32_t fast_cycle_ms;   /* período estimado do ciclo rápido (0 = taxa ainda desconhecida) */
    uint8_t load_source_pid;  /* 0x04, ou 0x11 se não houver 0x04; 0 se nenhum */
} obd_snapshot_t;

/* Devolve true se o carro suporta o PID. */
typedef bool (*obd_supported_fn)(void *arg, uint8_t pid);

typedef struct {
    uint8_t pid;
    uint32_t period_ms;  /* 0 = item rápido (round-robin) */
    uint32_t last_ms;    /* último pedido (só itens lentos) */
} obd_poll_item_t;

typedef struct {
    obd_poll_item_t items[OBD_POLLER_MAX_ITEMS];
    uint8_t n_items;
    uint8_t n_fast;      /* itens com period_ms == 0 */
    uint8_t rr;          /* próximo índice rápido, em round-robin */
    bool started;
    bool have_ok;
    uint32_t last_ok_ms;
    obd_snapshot_t snap;
} obd_poller_t;

/* supported pode ser NULL (tudo suportado). 0x0C e 0x0D sempre entram. extended agenda
   também MAP (0x0B) e IAT (0x0F), lentos, para a gravação da Etapa 2 (Kconfig
   OBD_POLL_EXTENDED no app; o componente não lê Kconfig). */
void    obd_poller_init(obd_poller_t *p, obd_supported_fn supported, void *arg, bool extended);
/* Próximo PID a pedir; 0 se a lista estiver vazia. */
uint8_t obd_poller_next(obd_poller_t *p, uint32_t now_ms);
void    obd_poller_record(obd_poller_t *p, uint8_t pid, float value, bool ok, uint32_t now_ms);
const obd_snapshot_t *obd_poller_snapshot(const obd_poller_t *p);

/* Modelo de taxa (puro): período de um ciclo rápido com n_fast itens a rate_hz respostas/s,
   arredondado para cima. 0 se a taxa ou a lista forem desconhecidas/vazias. */
uint32_t obd_poller_cycle_ms(float rate_hz, uint8_t n_fast);
