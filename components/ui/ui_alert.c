#include "ui_logic.h"

#define UI_TONE_MIN_INTERVAL_MS 3000u

void ui_alert_init(ui_alert_t *a)
{
    a->last = SHIFT_NONE;
    a->last_tone_ms = 0;
    a->armed = true;
    a->any_tone = false;
}

ui_tone_t ui_alert_update(ui_alert_t *a, shift_advice_t advice, uint32_t now_ms, bool muted)
{
    shift_advice_t prev = a->last;
    a->last = advice;                 /* acompanha mesmo mudo: ao reativar o som, nada velho dispara */
    a->armed = (advice == SHIFT_NONE);
    if (muted || advice == SHIFT_NONE || advice == prev) return UI_TONE_NONE;

    /* transição para UP/DOWN (inclusive UP<->DOWN); respeita o intervalo mínimo */
    if (a->any_tone && (uint32_t)(now_ms - a->last_tone_ms) < UI_TONE_MIN_INTERVAL_MS) return UI_TONE_NONE;
    a->any_tone = true;
    a->last_tone_ms = now_ms;
    return advice == SHIFT_UP ? UI_TONE_UP : UI_TONE_DOWN;
}
