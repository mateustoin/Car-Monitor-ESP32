#include "ui_logic.h"

ui_bg_t ui_bg_for(const gear_state_t *s, bool connected)
{
    if (!connected || s == NULL || s->stale) return UI_BG_NODATA;
    switch (s->advice) {
    case SHIFT_UP:   return UI_BG_UP;
    case SHIFT_DOWN: return UI_BG_DOWN;
    default:         return UI_BG_OK;
    }
}

uint32_t ui_bg_rgb(ui_bg_t bg)
{
    switch (bg) {
    case UI_BG_OK:   return 0x1B8A3A;
    case UI_BG_UP:   return 0x1565C0;
    case UI_BG_DOWN: return 0xEF6C00;
    default:         return 0x424242;
    }
}
