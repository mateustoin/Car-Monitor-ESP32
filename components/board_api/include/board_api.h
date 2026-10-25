#pragma once
/* API de placa: tela, buzzer, botões e sono profundo.
   Implementada pelo BSP escolhido em Kconfig (choice BOARD). */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

typedef enum { BOARD_BTN_A, BOARD_BTN_B, BOARD_BTN_PWR } board_button_t;
typedef enum { BOARD_PRESS_SHORT, BOARD_PRESS_LONG } board_press_t;   /* longo >= 2000 ms */
#define BOARD_LONG_PRESS_MS 2000

/* Chamado na task de varredura dos botões, ao soltar (curto) ou ao atingir 2000 ms (longo). */
typedef void (*board_button_cb_t)(board_button_t b, board_press_t p, void *arg);

esp_err_t     board_init(void);
lv_display_t *board_display(void);                 /* NULL no headless */
bool          board_lvgl_lock(uint32_t timeout_ms); /* 0 = espera indefinida */
void          board_lvgl_unlock(void);
esp_err_t     board_buzzer_tone(uint16_t freq_hz, uint16_t ms);   /* ESP_ERR_NOT_SUPPORTED no headless */
esp_err_t     board_buttons_register(board_button_cb_t cb, void *arg);
void          board_deep_sleep(uint32_t wake_after_ms);           /* acorda por timer ou BTN_A */
