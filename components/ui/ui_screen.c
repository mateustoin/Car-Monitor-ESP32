/* Tela LVGL: marcha grande, alvo da troca e faixa de status; e uma tela de diagnóstico.
   Layout e cor vêm da lógica pura (ui_layout_compute, ui_bg_for/ui_bg_rgb).
   Sem display (headless) tudo vira log, só quando o conteúdo muda. */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "board_api.h"
#include "esp_log.h"

static const char *TAG = "ui";

#define UI_LOCK_TIMEOUT_MS 100

static lv_display_t *s_disp;
static lv_obj_t *s_gear_view, *s_diag_view;
static lv_obj_t *s_gear_lbl, *s_target_lbl, *s_status_lbl, *s_diag_lbl;
static bool s_diag_active;

/* Último conteúdo mostrado, para não redesenhar/logar à toa. */
static struct {
    bool valid;
    ui_bg_t bg;
    char gear[8], target[16], status[40];
} s_last;
static char s_last_diag[192];

/* Maior fonte Montserrat habilitada no Kconfig do LVGL que cabe em max_px. */
static const lv_font_t *font_for_px(uint16_t max_px)
{
    static const struct { uint16_t px; const lv_font_t *font; } fonts[] = {
#if LV_FONT_MONTSERRAT_48
        {48, &lv_font_montserrat_48},
#endif
#if LV_FONT_MONTSERRAT_24
        {24, &lv_font_montserrat_24},
#endif
#if LV_FONT_MONTSERRAT_14
        {14, &lv_font_montserrat_14},
#endif
    };
    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        if (fonts[i].px <= max_px) return fonts[i].font;
    }
    return LV_FONT_DEFAULT;
}

static lv_obj_t *make_label(lv_obj_t *parent, ui_rect_t r, const lv_font_t *font, lv_text_align_t align)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, r.x, r.y);
    lv_obj_set_size(l, r.w, r.h);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_align(l, align, 0);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *make_view(lv_obj_t *scr, int32_t w, int32_t h)
{
    lv_obj_t *v = lv_obj_create(scr);
    lv_obj_remove_style_all(v);
    lv_obj_set_pos(v, 0, 0);
    lv_obj_set_size(v, w, h);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
    lv_obj_remove_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    return v;
}

/* Centraliza o texto na vertical dentro do retângulo pela altura da fonte. */
static void center_vert(lv_obj_t *l, ui_rect_t r, const lv_font_t *font)
{
    int32_t pad = (r.h - lv_font_get_line_height(font)) / 2;
    lv_obj_set_style_pad_top(l, pad > 0 ? pad : 0, 0);
}

esp_err_t ui_init(lv_display_t *disp)
{
    s_disp = disp;
    memset(&s_last, 0, sizeof(s_last));
    s_last_diag[0] = '\0';
    s_diag_active = false;
    if (disp == NULL) {
        ESP_LOGI(TAG, "sem display: interface só no log");
        return ESP_OK;
    }

    int32_t w = lv_display_get_horizontal_resolution(disp);
    int32_t h = lv_display_get_vertical_resolution(disp);
    ui_layout_t lay = ui_layout_compute((uint16_t)w, (uint16_t)h);
    uint16_t target_px = lay.target.h / 2 < lay.target.w / 2 ? lay.target.h / 2 : lay.target.w / 2;
    const lv_font_t *gear_font = font_for_px(lay.gear_font_px);
    const lv_font_t *target_font = font_for_px(target_px);
    const lv_font_t *status_font = font_for_px((uint16_t)lay.status.h);

    if (!board_lvgl_lock(0)) return ESP_ERR_TIMEOUT;
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    s_gear_view = make_view(scr, w, h);
    lv_obj_set_style_bg_color(s_gear_view, lv_color_hex(ui_bg_rgb(UI_BG_NODATA)), 0);
    s_gear_lbl = make_label(s_gear_view, lay.gear, gear_font, LV_TEXT_ALIGN_CENTER);
    center_vert(s_gear_lbl, lay.gear, gear_font);
    s_target_lbl = make_label(s_gear_view, lay.target, target_font, LV_TEXT_ALIGN_CENTER);
    center_vert(s_target_lbl, lay.target, target_font);
    s_status_lbl = make_label(s_gear_view, lay.status, status_font, LV_TEXT_ALIGN_CENTER);

    s_diag_view = make_view(scr, w, h);
    lv_obj_set_style_bg_color(s_diag_view, lv_color_black(), 0);
    ui_rect_t diag_r = {(int16_t)(w / 40), (int16_t)(h / 40), (int16_t)(w - w / 20), (int16_t)(h - h / 20)};
    s_diag_lbl = make_label(s_diag_view, diag_r, font_for_px(14), LV_TEXT_ALIGN_LEFT);
    lv_obj_add_flag(s_diag_view, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(s_gear_lbl, "-");
    lv_label_set_text(s_status_lbl, "iniciando");
    board_lvgl_unlock();
    ESP_LOGI(TAG, "tela %ldx%ld, fonte da marcha %u px (pedido %u)", (long)w, (long)h,
             (unsigned)lv_font_get_line_height(gear_font), (unsigned)lay.gear_font_px);
    return ESP_OK;
}

void ui_show_gear(const gear_state_t *s, bool connected)
{
    ui_bg_t bg = ui_bg_for(s, connected);
    char gear[8], target[16], status[40];

    if (s != NULL && s->gear > 0 && bg != UI_BG_NODATA) snprintf(gear, sizeof(gear), "%u", s->gear);
    else snprintf(gear, sizeof(gear), "-");

    if (bg == UI_BG_UP && s->target_gear > 0) snprintf(target, sizeof(target), LV_SYMBOL_UP "%u", s->target_gear);
    else if (bg == UI_BG_DOWN && s->target_gear > 0) snprintf(target, sizeof(target), LV_SYMBOL_DOWN "%u", s->target_gear);
    else target[0] = '\0';

    /* Textos da tela sem acento: as fontes embutidas do LVGL só têm ASCII. */
    if (!connected) snprintf(status, sizeof(status), "desconectado");
    else if (s == NULL || s->stale) snprintf(status, sizeof(status), "sem dados");
    else if (s->learning) snprintf(status, sizeof(status), "aprendendo (%u marchas)", s->learned_gears);
    else snprintf(status, sizeof(status), "%u marchas aprendidas", s->learned_gears);

    if (s_last.valid && s_last.bg == bg && strcmp(s_last.gear, gear) == 0 &&
        strcmp(s_last.target, target) == 0 && strcmp(s_last.status, status) == 0) {
        return;
    }
    s_last.valid = true;
    s_last.bg = bg;
    strcpy(s_last.gear, gear);
    strcpy(s_last.target, target);
    strcpy(s_last.status, status);

    if (s_disp == NULL) {
        if (bg == UI_BG_UP || bg == UI_BG_DOWN) {
            ESP_LOGI(TAG, "marcha %s, %s para %u | %s", gear, bg == UI_BG_UP ? "suba" : "reduza",
                     s->target_gear, status);
        } else {
            ESP_LOGI(TAG, "marcha %s | %s", gear, status);
        }
        return;
    }
    if (!board_lvgl_lock(UI_LOCK_TIMEOUT_MS)) {
        s_last.valid = false;   /* tenta de novo na próxima chamada */
        return;
    }
    lv_obj_set_style_bg_color(s_gear_view, lv_color_hex(ui_bg_rgb(bg)), 0);
    lv_label_set_text(s_gear_lbl, gear);
    lv_label_set_text(s_target_lbl, target);
    lv_label_set_text(s_status_lbl, status);
    board_lvgl_unlock();
}

void ui_show_diag(const ui_diag_t *d)
{
    if (d == NULL) return;
    char text[sizeof(s_last_diag)];
    snprintf(text, sizeof(text),
             "status: %s\nprotocolo: %s\nPIDs: %u  taxa: %.1f Hz\nRPM: %.0f  vel: %.0f km/h\nbateria: %.2f V",
             d->status ? d->status : "-", d->protocol ? d->protocol : "-", d->n_supported,
             (double)d->rate_hz, (double)d->rpm, (double)d->speed_kmh, (double)d->battery_v);
    if (strcmp(text, s_last_diag) == 0) return;
    strcpy(s_last_diag, text);

    if (s_disp == NULL) {
        ESP_LOGI(TAG, "diag: status=%s protocolo=%s PIDs=%u taxa=%.1fHz rpm=%.0f vel=%.0f bat=%.2fV",
                 d->status ? d->status : "-", d->protocol ? d->protocol : "-", d->n_supported,
                 (double)d->rate_hz, (double)d->rpm, (double)d->speed_kmh, (double)d->battery_v);
        return;
    }
    if (!board_lvgl_lock(UI_LOCK_TIMEOUT_MS)) {
        s_last_diag[0] = '\0';
        return;
    }
    lv_label_set_text(s_diag_lbl, text);
    board_lvgl_unlock();
}

void ui_toggle_screen(void)
{
    s_diag_active = !s_diag_active;
    ESP_LOGI(TAG, "tela: %s", s_diag_active ? "diagnóstico" : "marcha");
    if (s_disp == NULL) return;
    if (!board_lvgl_lock(UI_LOCK_TIMEOUT_MS)) {
        s_diag_active = !s_diag_active;
        return;
    }
    if (s_diag_active) {
        lv_obj_remove_flag(s_diag_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_gear_view, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_gear_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_diag_view, LV_OBJ_FLAG_HIDDEN);
    }
    board_lvgl_unlock();
}
