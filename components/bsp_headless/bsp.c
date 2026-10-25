/* BSP headless: sem tela, sem buzzer e sem botões; tudo vira log. */
#include "board_api.h"
#include "esp_log.h"
#include "esp_sleep.h"

static const char *TAG = "bsp_headless";

esp_err_t board_init(void)
{
    ESP_LOGI(TAG, "placa headless: sem tela, sem buzzer, sem botões");
    return ESP_OK;
}

lv_display_t *board_display(void)
{
    return NULL;
}

/* Sem LVGL rodando: não há o que travar. */
bool board_lvgl_lock(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return true;
}

void board_lvgl_unlock(void)
{
}

esp_err_t board_buzzer_tone(uint16_t freq_hz, uint16_t ms)
{
    (void)freq_hz;
    (void)ms;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t board_buttons_register(board_button_cb_t cb, void *arg)
{
    (void)cb;
    (void)arg;
    return ESP_ERR_NOT_SUPPORTED;
}

void board_deep_sleep(uint32_t wake_after_ms)
{
    ESP_LOGI(TAG, "sono profundo (timer %lu ms)", (unsigned long)wake_after_ms);
    if (wake_after_ms > 0) esp_sleep_enable_timer_wakeup((uint64_t)wake_after_ms * 1000);
    esp_deep_sleep_start();
}
