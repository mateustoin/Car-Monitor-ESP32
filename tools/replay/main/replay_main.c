/* Replay de uma gravação (.tsv) pelo elm327 + obd_poller + gear_engine reais.
   Uso: replay <arquivo.tsv> [profile_id] [--seeds a,b,c,...]
   Saída: CSV t_ms,rpm,speed,load,gear,advice,target,learning (relógio = do replay). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elm327.h"
#include "gear_engine.h"
#include "obd_poller.h"
#include "transport_replay.h"
#include "vehicle_profile.h"

#define MAX_NO_DATA 1000   /* teto de NO DATA seguidos (gravação sem um PID pedido) */

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    if (buf) buf[n] = '\0';
    fclose(f);
    return buf;
}

static bool supported(void *arg, uint8_t pid) { return elm327_pid_supported(arg, pid); }

static const char *advice_name(shift_advice_t a)
{
    return a == SHIFT_UP ? "UP" : a == SHIFT_DOWN ? "DOWN" : "NONE";
}

int main(int argc, char **argv)
{
    const char *path = NULL, *pid_name = "sandero_k7m", *seeds_arg = NULL;
    int npos = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--seeds") == 0 && i + 1 < argc) seeds_arg = argv[++i];
        else if (npos == 0) { path = argv[i]; npos++; }
        else if (npos == 1) { pid_name = argv[i]; npos++; }
        else path = NULL;
    }
    if (!path) {
        fprintf(stderr, "uso: %s <arquivo.tsv> [profile_id] [--seeds a,b,c,...]\n", argv[0]);
        return 2;
    }
    const vehicle_profile_t *prof = vp_find(pid_name);
    if (!prof) { fprintf(stderr, "perfil desconhecido: %s\n", pid_name); return 2; }

    float seeds[VP_MAX_GEARS];
    if (seeds_arg) {
        int n = 0;
        char *copy = strdup(seeds_arg), *save = NULL;
        for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            if (n >= VP_MAX_GEARS) { n++; break; }
            char *end;
            seeds[n] = strtof(tok, &end);
            if (end == tok || *end != '\0') {
                fprintf(stderr, "--seeds: valor inválido \"%s\"\n", tok);
                return 2;
            }
            n++;
        }
        free(copy);
        if (n != prof->gears) {
            fprintf(stderr, "--seeds: %d valores, o perfil %s tem %u marchas\n", n, prof->id, prof->gears);
            return 2;
        }
    }

    char *rec = read_file(path);
    if (!rec) { fprintf(stderr, "não foi possível ler %s\n", path); return 1; }
    obd_transport_t *t;
    if (transport_replay_create(rec, 0, &t) != ESP_OK) { fprintf(stderr, "falha ao criar o replay\n"); return 1; }
    free(rec);
    t->ops->connect(t, 100);

    elm327_t elm;
    if (elm327_init(&elm, t, 1000) != ESP_OK) { fprintf(stderr, "elm327_init falhou\n"); return 1; }
    obd_poller_t poller;
    obd_poller_init(&poller, supported, &elm, false);
    gear_engine_t g;
    gear_engine_init(&g, prof, seeds_arg ? seeds : NULL);

    printf("t_ms,rpm,speed,load,gear,advice,target,learning\n");
    int no_data = 0;
    uint32_t prev_now = 0, last_rpm_t = UINT32_MAX, last_spd_t = UINT32_MAX;
    while (!transport_replay_eof(t) && no_data < MAX_NO_DATA) {
        uint32_t now = transport_replay_now_ms(t);
        uint8_t pid = obd_poller_next(&poller, now);
        if (pid == 0) break;
        float v = 0;
        esp_err_t err = elm327_read_pid(&elm, pid, &v);
        now = transport_replay_now_ms(t);
        if (now - prev_now > gear_engine_stale_ms(&g))
            fprintf(stderr, "aviso: relógio saltou %u ms (registros pulados na gravação?) em t=%u\n",
                    (unsigned)(now - prev_now), (unsigned)now);
        prev_now = now;
        no_data = (err == ESP_ERR_NOT_FOUND) ? no_data + 1 : 0;
        obd_poller_record(&poller, pid, v, err == ESP_OK, now);

        const obd_snapshot_t *s = obd_poller_snapshot(&poller);
        gear_engine_set_poll_cycle(&g, s->fast_cycle_ms);   /* como no app (R34) */
        if (!s->rpm.valid || !s->speed_kmh.valid) continue;
        if (s->rpm.t_ms == last_rpm_t && s->speed_kmh.t_ms == last_spd_t) continue;
        last_rpm_t = s->rpm.t_ms;
        last_spd_t = s->speed_kmh.t_ms;
        gear_input_t in = {
            .now_ms = now,
            .sample_ms = s->rpm.t_ms < s->speed_kmh.t_ms ? s->rpm.t_ms : s->speed_kmh.t_ms,
            .rpm = s->rpm.value, .speed_kmh = s->speed_kmh.value,
            .load_pct = s->load_pct.valid ? s->load_pct.value : 0,
            .throttle_pct = s->throttle_pct.valid ? s->throttle_pct.value : NAN,
        };
        gear_state_t st = gear_engine_update(&g, &in);
        printf("%u,%.0f,%.0f,%.0f,%u,%s,%u,%d\n", (unsigned)now, in.rpm, in.speed_kmh, in.load_pct,
               st.gear, advice_name(st.advice), st.target_gear, st.learning ? 1 : 0);
    }
    t->ops->destroy(t);
    return 0;
}

/* O port linux do FreeRTOS exige app_main; o trabalho todo roda em main(). */
void app_main(void) {}
