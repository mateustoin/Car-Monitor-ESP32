/* Gravação de uma viagem: cada troca ELM327 (cmd/resp) vira uma linha do formato de
   rec_format.c, enviada à serial ("REC\t<linha>") e/ou a /littlefs/trip_NNN.tsv.
   rec_tap roda na tarefa OBD; os comandos de console rodam na tarefa do REPL; o estado
   dos sinks fica sob um mutex e rec_tap desiste da linha se não o obtiver logo. */
#include "obd_recorder.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_console.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define MOUNT_POINT     "/littlefs"
#define PARTITION_LABEL "storage"
#define LINE_CAP        256
#define FLUSH_MS        2000
#define TAP_LOCK_MS     5

static const char *TAG = "obd_recorder";

static SemaphoreHandle_t s_lock;
static bool s_mounted;
static int s_sinks;               /* máscara de rec_sink_t ativa */
static FILE *s_file;
static int s_file_index = -1;
static uint32_t s_last_flush_ms;
static volatile uint32_t s_lines, s_drops;

static bool lock_take(uint32_t ms) { return s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(ms)) == pdTRUE; }
static void lock_give(void) { xSemaphoreGive(s_lock); }

static void trip_path(char *out, size_t cap, int index)
{
    snprintf(out, cap, MOUNT_POINT "/trip_%03d.tsv", index);
}

/* Próximo índice livre: 1 + maior NNN existente (0 se não há viagens); -1 se o diretório
   não abre (não arriscar sobrescrever trip_000.tsv). */
static int next_trip_index(void)
{
    int max = -1;
    DIR *d = opendir(MOUNT_POINT);
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        int n = rec_trip_index(e->d_name);
        if (n > max) max = n;
    }
    closedir(d);
    return max + 1;
}

esp_err_t rec_init(void)
{
    if (s_mounted) return ESP_OK;
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return ESP_ERR_NO_MEM;
    }
    esp_vfs_littlefs_conf_t conf = {
        .base_path = MOUNT_POINT,
        .partition_label = PARTITION_LABEL,
        .format_if_mount_failed = false,
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "montagem falhou (%s): FORMATANDO a partição \"%s\", viagens antigas perdidas",
                 esp_err_to_name(err), PARTITION_LABEL);
        conf.format_if_mount_failed = true;
        err = esp_vfs_littlefs_register(&conf);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "falha ao montar LittleFS: %s", esp_err_to_name(err));
        return err;
    }
    s_mounted = true;
    return ESP_OK;
}

/* Chamar com o mutex tomado. */
static void close_file_locked(void)
{
    if (s_file) {
        fclose(s_file);
        s_file = NULL;
    }
    s_sinks &= ~REC_SINK_FLASH;
}

esp_err_t rec_start(int sinks)
{
    sinks &= REC_SINK_SERIAL | REC_SINK_FLASH;
    if (sinks == 0) return ESP_ERR_INVALID_ARG;
    if ((sinks & REC_SINK_FLASH) && !s_mounted) return ESP_ERR_INVALID_STATE;
    if (!lock_take(1000)) return ESP_ERR_TIMEOUT;
    if (s_sinks) {
        lock_give();
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_OK;
    if (sinks & REC_SINK_FLASH) {
        int idx = next_trip_index();
        if (idx < 0) {
            ESP_LOGE(TAG, "não foi possível listar " MOUNT_POINT);
            lock_give();
            return ESP_FAIL;
        }
        char path[40];
        trip_path(path, sizeof(path), idx);
        s_file = fopen(path, "w");
        if (!s_file) {
            ESP_LOGE(TAG, "não foi possível criar %s (errno %d)", path, errno);
            err = ESP_FAIL;
        } else {
            s_file_index = idx;
            ESP_LOGI(TAG, "gravando em %s", path);
        }
    }
    if (err == ESP_OK) {
        s_sinks = sinks;
        s_lines = 0;
        s_drops = 0;
        s_last_flush_ms = esp_log_timestamp();
    }
    lock_give();
    return err;
}

/* Leitura sem mutex: um int alinhado; quem chama só quer saber se a gravação começou. */
bool rec_active(void)
{
    return s_sinks != 0;
}

void rec_stop(void)
{
    if (!lock_take(1000)) {
        ESP_LOGW(TAG, "rec_stop: mutex ocupado, gravação não foi parada");
        return;
    }
    uint32_t lines = s_lines, drops = s_drops;
    bool was_active = s_sinks != 0;
    close_file_locked();
    s_sinks = 0;
    lock_give();
    if (was_active) ESP_LOGI(TAG, "gravação parada: %lu linhas, %lu descartadas",
                             (unsigned long)lines, (unsigned long)drops);
}

void rec_tap(void *arg, const char *cmd, const char *resp)
{
    (void)arg;
    if (!s_lock || !cmd || !resp) return;
    if (!lock_take(TAP_LOCK_MS)) {
        s_drops++;   /* contagem aproximada: só é lida pelo console */
        return;
    }
    if (s_sinks) {
        uint32_t now = esp_log_timestamp();
        char line[LINE_CAP];
        if (rec_format_line(line, sizeof(line), now, cmd, resp) < 0) {
            s_drops++;
        } else {
            s_lines++;
            if (s_sinks & REC_SINK_SERIAL) printf("REC\t%s", line);
            if ((s_sinks & REC_SINK_FLASH) && s_file) {
                size_t len = strlen(line);
                bool bad = fwrite(line, 1, len, s_file) != len;
                if (!bad && now - s_last_flush_ms >= FLUSH_MS) {
                    /* fflush não basta no LittleFS: os metadados só são confirmados no sync */
                    bad = fflush(s_file) != 0 || fsync(fileno(s_file)) != 0;
                    s_last_flush_ms = now;
                }
                if (bad) {
                    ESP_LOGW(TAG, "falha ao gravar na flash (errno %d), provavelmente cheia: "
                                  "sink de flash desligado, serial continua", errno);
                    close_file_locked();
                    if (!s_sinks) ESP_LOGW(TAG, "nenhum sink ativo");
                }
            }
        }
    }
    lock_give();
}

/* ---- console ---- */

static void cmd_ls(void)
{
    DIR *d = opendir(MOUNT_POINT);
    if (!d) {
        printf("sem acesso a " MOUNT_POINT "\n");
        return;
    }
    struct dirent *e;
    int count = 0;
    while ((e = readdir(d)) != NULL) {
        int n = rec_trip_index(e->d_name);
        if (n < 0) continue;
        char path[300];
        struct stat st;
        snprintf(path, sizeof(path), MOUNT_POINT "/%s", e->d_name);
        long size = stat(path, &st) == 0 ? (long)st.st_size : -1;
        printf("%d\t%s\t%ld bytes\n", n, e->d_name, size);
        count++;
    }
    closedir(d);
    size_t total = 0, used = 0;
    if (esp_littlefs_info(PARTITION_LABEL, &total, &used) == ESP_OK) {
        printf("%d viagem(ns); flash %u/%u bytes usados\n", count, (unsigned)used, (unsigned)total);
    }
    if (lock_take(100)) {
        printf("gravando: %s%s; %lu linhas, %lu descartadas\n",
               s_sinks ? "sim" : "não",
               (s_sinks & REC_SINK_SERIAL) ? " serial" : "",
               (unsigned long)s_lines, (unsigned long)s_drops);
        lock_give();
    }
}

static void cmd_dump(int index)
{
    /* Se é a viagem em gravação, empurra o buffer antes de ler. */
    if (lock_take(100)) {
        if (s_file && s_file_index == index) fflush(s_file);
        lock_give();
    }
    char path[40];
    trip_path(path, sizeof(path), index);
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("viagem %d não encontrada\n", index);
        return;
    }
    char buf[LINE_CAP + 2];
    bool at_start = true;
    while (fgets(buf, sizeof(buf), f)) {
        if (at_start) fputs("REC\t", stdout);
        fputs(buf, stdout);
        at_start = strchr(buf, '\n') != NULL;
    }
    fclose(f);
}

static void cmd_clear(void)
{
    if (!lock_take(100)) {
        printf("gravador ocupado, tente de novo\n");
        return;
    }
    bool busy = (s_sinks & REC_SINK_FLASH) != 0;
    lock_give();
    if (busy) {
        printf("pare a gravação antes (rec off)\n");
        return;
    }
    DIR *d = opendir(MOUNT_POINT);
    if (!d) return;
    struct dirent *e;
    int removed = 0;
    while ((e = readdir(d)) != NULL) {
        if (rec_trip_index(e->d_name) < 0) continue;
        char path[300];
        snprintf(path, sizeof(path), MOUNT_POINT "/%s", e->d_name);
        if (unlink(path) == 0) removed++;
    }
    closedir(d);
    printf("%d arquivo(s) apagado(s)\n", removed);
}

static int cmd_rec(int argc, char **argv)
{
    const char *usage = "uso: rec on [serial|flash|both] | rec off | rec ls | rec dump <n> | rec clear\n";
    if (argc < 2) {
        printf("%s", usage);
        return 1;
    }
    if (strcmp(argv[1], "on") == 0) {
        int sinks = REC_SINK_SERIAL | REC_SINK_FLASH;
        if (argc >= 3) {
            if (strcmp(argv[2], "serial") == 0) sinks = REC_SINK_SERIAL;
            else if (strcmp(argv[2], "flash") == 0) sinks = REC_SINK_FLASH;
            else if (strcmp(argv[2], "both") != 0) {
                printf("%s", usage);
                return 1;
            }
        }
        esp_err_t err = rec_start(sinks);
        if (err != ESP_OK) {
            printf("não foi possível iniciar: %s\n", esp_err_to_name(err));
            return 1;
        }
        printf("gravando\n");
    } else if (strcmp(argv[1], "off") == 0) {
        rec_stop();
        printf("parado\n");
    } else if (strcmp(argv[1], "ls") == 0) {
        cmd_ls();
    } else if (strcmp(argv[1], "dump") == 0 && argc >= 3) {
        char *end;
        long n = strtol(argv[2], &end, 10);
        if (*end != '\0' || n < 0) {
            printf("%s", usage);
            return 1;
        }
        cmd_dump((int)n);
    } else if (strcmp(argv[1], "clear") == 0) {
        cmd_clear();
    } else {
        printf("%s", usage);
        return 1;
    }
    return 0;
}

esp_err_t rec_console_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "rec",
        .help = "Grava a viagem: rec on [serial|flash|both], rec off, rec ls, rec dump <n>, rec clear",
        .hint = NULL,
        .func = &cmd_rec,
    };
    return esp_console_cmd_register(&cmd);
}
