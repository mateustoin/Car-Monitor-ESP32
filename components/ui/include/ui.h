#pragma once
/* Tela da interface (LVGL). Só existe fora do alvo linux; a lógica pura fica em ui_logic.h. */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"
#include "gear_engine.h"
#include "ui_logic.h"

typedef struct {
    const char *status;     /* p.ex. "conectado", "procurando ELM" */
    const char *protocol;   /* protocolo OBD informado pelo ELM */
    uint8_t n_supported;    /* nº de PIDs suportados */
    float rate_hz;          /* taxa de amostras */
    float rpm, speed_kmh, battery_v;
} ui_diag_t;

/* disp NULL = modo só log (headless). Chama board_lvgl_lock internamente. */
esp_err_t ui_init(lv_display_t *disp);
void      ui_show_gear(const gear_state_t *s, bool connected);
void      ui_show_diag(const ui_diag_t *d);
/* Alterna entre a tela de marcha e a de diagnóstico (sem efeito se o lock do LVGL não vier). */
void      ui_toggle_screen(void);
/* true se a tela visível é a de diagnóstico. */
bool      ui_screen_is_diag(void);
