/* Assistente de troca de marchas: inicializa NVS, placa, tela e console, e sobe as tasks
   app_obd (ELM327) e app_ui (gear_engine, tela, bipe, botões e energia). */
#include "esp_console.h"
#include "esp_log.h"
#include "app_obd.h"
#include "app_ui.h"
#include "board_api.h"
#include "obd_recorder.h"
#include "storage.h"
#include "ui.h"

static const char *TAG = "car_monitor";

/* Console na UART0 com o comando "rec" (gravação da viagem; ver obd_recorder). */
static void start_console(void)
{
    /* Sem LittleFS ainda dá para gravar só na serial, então o console sobe mesmo assim. */
    esp_err_t err = rec_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "flash indisponível, só gravação serial: %s", esp_err_to_name(err));
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
    ESP_LOGI(TAG, "car_monitor: assistente de marchas");
    ESP_ERROR_CHECK(storage_init());   /* antes do BT: o Bluedroid guarda o pareamento no NVS */
    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(ui_init(board_display()));
    start_console();
    ESP_ERROR_CHECK(app_obd_start());
    ESP_ERROR_CHECK(app_ui_start());
}
