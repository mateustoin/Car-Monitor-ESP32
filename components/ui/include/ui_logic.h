#pragma once
/* Lógica pura da interface: layout adaptável, cor de fundo e política de bipe.
   Sem LVGL nem hardware; o tempo vem sempre em now_ms. */
#include <stdbool.h>
#include <stdint.h>
#include "gear_engine.h"

typedef struct { int16_t x, y, w, h; } ui_rect_t;
typedef struct { ui_rect_t gear, target, status; uint16_t gear_font_px; } ui_layout_t;

/* Calcula os retângulos para qualquer resolução (paisagem: alvo à direita; retrato: abaixo). */
ui_layout_t ui_layout_compute(uint16_t width, uint16_t height);

typedef enum { UI_BG_OK, UI_BG_UP, UI_BG_DOWN, UI_BG_NODATA } ui_bg_t;

/* NODATA se desconectado, estado nulo ou amostra velha; senão pelo conselho. */
ui_bg_t  ui_bg_for(const gear_state_t *s, bool connected);
uint32_t ui_bg_rgb(ui_bg_t bg);   /* 0x1B8A3A, 0x1565C0, 0xEF6C00, 0x424242 */

typedef enum { UI_TONE_NONE, UI_TONE_UP, UI_TONE_DOWN } ui_tone_t;
typedef struct {
    shift_advice_t last;      /* último conselho visto */
    shift_advice_t toned;     /* conselho do último evento (tocado, mudo ou suprimido) */
    uint8_t  gear;            /* marcha do último update */
    uint32_t none_since_ms;   /* início do NONE atual */
    uint32_t last_tone_ms;
    bool armed;               /* pode tocar de novo o mesmo conselho */
    bool any_tone;            /* já tocou algum bipe (last_tone_ms só vale se true) */
} ui_alert_t;

#define UI_TONE_MIN_INTERVAL_MS 3000u
#define UI_TONE_REARM_NONE_MS   2000u

void      ui_alert_init(ui_alert_t *a);
/* Um bipe por evento (R33). Evento = conselho UP/DOWN com o alerta armado, ou um conselho
   diferente do último evento (UP<->DOWN). Rearma só depois de NONE por >= 2000 ms ou de uma
   troca de marcha. Intervalo mínimo de 3000 ms entre bipes. Evento suprimido (mudo ou
   intervalo) é consumido: não toca depois sozinho. */
ui_tone_t ui_alert_update(ui_alert_t *a, shift_advice_t advice, uint8_t gear, uint32_t now_ms, bool muted);
