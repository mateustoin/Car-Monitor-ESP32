#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Formato de linha: "t_ms\tCMD\tRESP\n". Os CR internos de RESP viram '|'. */

/* Escreve a linha em out. Retorna bytes escritos (sem contar o '\0') ou -1 se não couber. */
int  rec_format_line(char *out, size_t cap, uint32_t t_ms, const char *cmd, const char *resp);

/* Lê uma linha (com ou sem '\n' final). '|' em RESP volta a ser '\r'. false se malformada
   ou se cmd/resp não couberem. */
bool rec_parse_line(const char *line, uint32_t *t_ms, char *cmd, size_t cmd_cap,
                    char *resp, size_t resp_cap);

/* Nome de arquivo de viagem "trip_NNN.tsv" -> NNN. -1 se o nome não segue o padrão. */
int  rec_trip_index(const char *name);

/* Gravação em campo (só fora do alvo linux; implementada em obd_recorder.c).
   Os tipos aqui não puxam headers de hardware. */
typedef enum { REC_SINK_SERIAL = 1, REC_SINK_FLASH = 2 } rec_sink_t;

esp_err_t rec_init(void);                      /* monta LittleFS da partição "storage" em /littlefs */
esp_err_t rec_start(int sinks);                /* cria /littlefs/trip_NNN.tsv se REC_SINK_FLASH */
void rec_tap(void *arg, const char *cmd, const char *resp); /* compatível com elm327_tap_t */
void rec_stop(void);
esp_err_t rec_console_register(void);          /* "rec on|off|ls|dump|clear" */
