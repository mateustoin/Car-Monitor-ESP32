#include "ui_logic.h"

void ui_alert_init(ui_alert_t *a)
{
    a->last = SHIFT_NONE;
    a->toned = SHIFT_NONE;
    a->gear = 0;
    a->none_since_ms = 0;
    a->last_tone_ms = 0;
    a->armed = true;
    a->any_tone = false;
}

ui_tone_t ui_alert_update(ui_alert_t *a, shift_advice_t advice, uint8_t gear, uint32_t now_ms, bool muted)
{
    shift_advice_t prev = a->last;
    a->last = advice;   /* acompanha mesmo mudo: ao reativar o som, nada velho dispara */

    if (gear != a->gear) {   /* trocou de marcha: o próximo conselho é um evento novo */
        a->gear = gear;
        a->armed = true;
    }
    if (advice == SHIFT_NONE) {
        if (prev != SHIFT_NONE) a->none_since_ms = now_ms;
        if ((uint32_t)(now_ms - a->none_since_ms) >= UI_TONE_REARM_NONE_MS) a->armed = true;
        return UI_TONE_NONE;
    }
    if (!a->armed && advice == a->toned) return UI_TONE_NONE;

    /* evento novo: consumido aqui, toque ou não */
    a->armed = false;
    a->toned = advice;
    if (muted) return UI_TONE_NONE;
    if (a->any_tone && (uint32_t)(now_ms - a->last_tone_ms) < UI_TONE_MIN_INTERVAL_MS) return UI_TONE_NONE;
    a->any_tone = true;
    a->last_tone_ms = now_ms;
    return advice == SHIFT_UP ? UI_TONE_UP : UI_TONE_DOWN;
}
