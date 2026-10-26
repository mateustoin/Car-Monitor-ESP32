#pragma once
/* Task da interface (20 Hz): gear_engine, telas, bipe, botões e decisão de sono. */
#include "esp_err.h"

/* board_init, ui_init e app_obd_start já feitos. Registra os botões e cria a task. */
esp_err_t app_ui_start(void);
