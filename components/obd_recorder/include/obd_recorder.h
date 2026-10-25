#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Formato de linha: "t_ms\tCMD\tRESP\n". Os CR internos de RESP viram '|'. */

/* Escreve a linha em out. Retorna bytes escritos (sem contar o '\0') ou -1 se não couber. */
int  rec_format_line(char *out, size_t cap, uint32_t t_ms, const char *cmd, const char *resp);

/* Lê uma linha (com ou sem '\n' final). '|' em RESP volta a ser '\r'. false se malformada
   ou se cmd/resp não couberem. */
bool rec_parse_line(const char *line, uint32_t *t_ms, char *cmd, size_t cmd_cap,
                    char *resp, size_t resp_cap);
