#include "ui_logic.h"

/* Tudo proporcional à resolução: nenhuma constante de tela fixa.
   Faixa de status = 1/8 da altura; a marcha ocupa 80% da área restante (>= 70% da tela). */
ui_layout_t ui_layout_compute(uint16_t width, uint16_t height)
{
    ui_layout_t l;
    int w = width, h = height;
    int status_h = h / 8 > 0 ? h / 8 : 1;
    int main_h = h - status_h;
    int margin = w / 20;
    int gear_h = main_h * 80 / 100;

    l.status = (ui_rect_t){0, (int16_t)main_h, (int16_t)w, (int16_t)status_h};
    if (w >= h) {
        /* paisagem: marcha à esquerda do centro, alvo à direita */
        int gear_w = w * 55 / 100;
        int y = (main_h - gear_h) / 2;
        int tx = margin + gear_w + margin;
        l.gear = (ui_rect_t){(int16_t)margin, (int16_t)y, (int16_t)gear_w, (int16_t)gear_h};
        l.target = (ui_rect_t){(int16_t)tx, (int16_t)y, (int16_t)(w - tx - margin), (int16_t)gear_h};
    } else {
        /* retrato: marcha no topo, alvo abaixo */
        int y = main_h / 20;
        int ty = y + gear_h;
        l.gear = (ui_rect_t){(int16_t)margin, (int16_t)y, (int16_t)(w - 2 * margin), (int16_t)gear_h};
        l.target = (ui_rect_t){(int16_t)margin, (int16_t)ty, (int16_t)(w - 2 * margin), (int16_t)(main_h - ty)};
    }
    l.gear_font_px = (uint16_t)(gear_h * 85 / 100);
    return l;
}
