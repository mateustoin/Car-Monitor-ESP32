/* Demo temporária de hardware (Tarefa 12; substituída na Tarefa 16): cicla os 4 fundos
   com uma marcha falsa a cada 2 s, toca os tons de subir e reduzir e loga os botões. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "esp_log.h"
#include "board_api.h"
#include "ui.h"
#include "obd_recorder.h"

static const char *TAG = "car_monitor";

static void on_button(board_button_t b, board_press_t p, void *arg)
{
    (void)arg;
    static const char *const names[] = {"A", "B", "PWR"};
    ESP_LOGI(TAG, "botão %s: %s", names[b], p == BOARD_PRESS_LONG ? "longo" : "curto");
}

/* Dois bipes: subindo para "suba", descendo para "reduza". */
static void play_tone(bool up)
{
    if (board_buzzer_tone(up ? 1800 : 2600, 90) != ESP_OK) return;
    vTaskDelay(pdMS_TO_TICKS(130));
    board_buzzer_tone(up ? 2600 : 1600, 90);
}

/* Console na UART0 com o comando "rec" (gravação da viagem; ver obd_recorder). */
static void start_console(void)
{
    esp_err_t err = rec_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "gravador indisponível: %s", esp_err_to_name(err));
        return;
    }
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "carmon> ";
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    err = esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl);
    if (err == ESP_OK) err = rec_console_register();
    if (err == ESP_OK) err = esp_console_start_repl(repl);
    if (err != ESP_OK) ESP_LOGW(TAG, "console indisponível: %s", esp_err_to_name(err));
}

void app_main(void)
{
    ESP_LOGI(TAG, "car_monitor: demo de hardware");
    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(ui_init(board_display()));
    esp_err_t err = board_buttons_register(on_button, NULL);
    if (err != ESP_OK) ESP_LOGW(TAG, "botões indisponíveis: %s", esp_err_to_name(err));

    start_console();

    static const struct { gear_state_t s; bool connected; const char *name; } steps[] = {
        {{.gear = 3, .advice = SHIFT_NONE, .learned_gears = 5}, true, "OK"},
        {{.gear = 2, .advice = SHIFT_UP, .target_gear = 3, .learned_gears = 5}, true, "UP"},
        {{.gear = 4, .advice = SHIFT_DOWN, .target_gear = 3, .learned_gears = 5}, true, "DOWN"},
        {{.gear = 0, .advice = SHIFT_NONE, .stale = true}, false, "NODATA"},
    };
    for (unsigned i = 0;; i = (i + 1) % (sizeof(steps) / sizeof(steps[0]))) {
        ESP_LOGI(TAG, "fundo %s", steps[i].name);
        ui_show_gear(&steps[i].s, steps[i].connected);
        if (steps[i].s.advice == SHIFT_UP) play_tone(true);
        else if (steps[i].s.advice == SHIFT_DOWN) play_tone(false);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
