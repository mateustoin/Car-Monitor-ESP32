/* Task OBD: transporte → elm327 → obd_poller, com reconexão em backoff e status sob mutex.
   R32: a sessão só derruba o link e reconecta com erro de transporte (ESP_ERR_INVALID_STATE,
   ESP_FAIL) ou link caído; erros do ELM (ESP_ERR_INVALID_RESPONSE, timeout com o link de pé)
   são contados por elm327_health e, 3 seguidos, refazem o elm327_init no mesmo link. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "app_obd.h"
#include "elm327.h"
#include "obd_recorder.h"
#include "storage.h"

#define OBD_TASK_PRIO       5
#define OBD_TASK_STACK      6144
#define CONNECT_TIMEOUT_MS  16000   /* cobre a busca completa do SPP (ver transport_bt_spp.h) */
#define ELM_TIMEOUT_MS      2000
#define ATRV_PERIOD_MS      10000
#define BACKOFF_MIN_MS      2000
#define BACKOFF_MAX_MS      30000
#define INIT_CACHE_LINES    12      /* ATZ..ATSP0, 0100, ATDPN, 0120, 0140 e folga */
#if CONFIG_OBD_POLL_EXTENDED
#define POLL_EXTENDED       true    /* MAP/IAT na agenda (gravação da Etapa 2) */
#else
#define POLL_EXTENDED       false
#endif

static const char *TAG = "app_obd";

static SemaphoreHandle_t s_lock;
static app_obd_status_t s_status;
static TaskHandle_t s_task;
static volatile bool s_repair;

static void status_lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void status_unlock(void) { xSemaphoreGive(s_lock); }

void app_obd_get(app_obd_status_t *out)
{
    if (!s_lock) {
        memset(out, 0, sizeof(*out));
        out->state = APP_OBD_NO_TRANSPORT;
        out->battery_v = NAN;
        return;
    }
    status_lock();
    *out = s_status;
    status_unlock();
}

uint32_t app_obd_stack_free(void)
{
    TaskHandle_t task = s_task;
    return task ? uxTaskGetStackHighWaterMark(task) : 0;
}

void app_obd_request_repair(void)
{
    s_repair = true;
    if (s_task) xTaskNotifyGive(s_task);   /* interrompe a espera do backoff */
}

static void set_state(app_obd_state_t state)
{
    status_lock();
    s_status.state = state;
    s_status.connected = state == APP_OBD_CONNECTED;
    if (!s_status.connected) s_status.battery_v = NAN;
    status_unlock();
}

/* ---- Gravação: a inicialização da sessão fica guardada e é repetida no início de uma
   gravação ligada no meio da sessão; sem ela o replay não passa do elm327_init. ---- */
static struct { char cmd[12]; char resp[128]; } s_init_lines[INIT_CACHE_LINES];
static int s_init_n;
static bool s_in_init, s_rec_was_active;

static void obd_tap(void *arg, const char *cmd, const char *resp)
{
    bool cached = false;
    if (s_in_init && s_init_n < INIT_CACHE_LINES) {
        strlcpy(s_init_lines[s_init_n].cmd, cmd, sizeof(s_init_lines[0].cmd));
        strlcpy(s_init_lines[s_init_n].resp, resp, sizeof(s_init_lines[0].resp));
        s_init_n++;
        cached = true;
    }
    bool active = rec_active();
    bool started = active && !s_rec_was_active;
    s_rec_was_active = active;
    if (started) {
        for (int i = 0; i < s_init_n; i++) rec_tap(arg, s_init_lines[i].cmd, s_init_lines[i].resp);
        if (cached) return;   /* a linha atual já saiu com o cache */
    }
    rec_tap(arg, cmd, resp);   /* no-op sem gravação */
}

static bool pid_supported(void *arg, uint8_t pid)
{
    return elm327_pid_supported(arg, pid);
}

static uint8_t count_supported(const elm327_t *elm)
{
    uint8_t n = 0;
    for (unsigned pid = 1; pid <= 0x60; pid++) {
        if (pid % 0x20 == 0) continue;   /* 0x20/0x40/0x60 só anunciam a próxima faixa */
        if (elm327_pid_supported(elm, (uint8_t)pid)) n++;
    }
    return n;
}

static void publish_session(const elm327_t *elm, const obd_poller_t *poller)
{
    status_lock();
    s_status.state = APP_OBD_CONNECTED;
    s_status.connected = true;
    s_status.snap = *obd_poller_snapshot(poller);
    strlcpy(s_status.protocol, elm->protocol, sizeof(s_status.protocol));
    s_status.n_supported = count_supported(elm);
    status_unlock();
}

static void publish_sample(const obd_poller_t *poller, uint8_t pid, float value, bool ok, uint32_t now)
{
    status_lock();
    s_status.snap = *obd_poller_snapshot(poller);
    if (ok) s_status.last_elm_ok_ms = now;
    if (pid == ELM_PID_RPM) s_status.rpm_answers++;   /* só chega aqui com valor ou NO DATA */
    if (ok && pid == ELM_PID_RPM && value > 0) s_status.last_engine_running_ms = now;
    status_unlock();
}

/* Gravação automática na flash (APP_REC_AUTOSTART): começa no primeiro RPM > 0 desde o boot,
   e não na conexão, para os acordes do timer com o carro desligado não criarem viagens vazias.
   Uma vez por boot: um "rec off" no console não é desfeito. */
static void rec_autostart(uint8_t pid, float value, bool ok)
{
#if CONFIG_APP_REC_AUTOSTART
    static bool done;
    if (done || !ok || pid != ELM_PID_RPM || value <= 0) return;
    done = true;
    if (rec_active()) return;   /* já ligada pelo console */
    esp_err_t err = rec_start(REC_SINK_FLASH);
    if (err == ESP_OK) ESP_LOGI(TAG, "gravação automática na flash iniciada (motor ligado)");
    else ESP_LOGW(TAG, "gravação automática não iniciou: %s", esp_err_to_name(err));
#else
    (void)pid;
    (void)value;
    (void)ok;
#endif
}

static void publish_battery(float v)
{
    status_lock();
    s_status.battery_v = v;
    status_unlock();
}

/* Inicialização do ELM com a troca guardada para a gravação. */
static esp_err_t elm_init(obd_transport_t *t, elm327_t *elm)
{
    s_init_n = 0;
    s_in_init = true;
    esp_err_t err = elm327_init_with_tap(elm, t, ELM_TIMEOUT_MS, obd_tap, NULL);
    s_in_init = false;
    if (err != ESP_OK) ESP_LOGW(TAG, "elm327_init: %s", esp_err_to_name(err));
    return err;
}

/* Agenda nova para o ELM recém-inicializado (a máscara de PIDs pode ter mudado). */
static void start_polling(obd_poller_t *poller, const elm327_t *elm)
{
    obd_poller_init(poller, pid_supported, (void *)elm, POLL_EXTENDED);
    publish_session(elm, poller);
}

/* Aplica a decisão do elm327_health a um resultado. ESP_OK = segue a sessão; outro valor =
   a sessão acaba com esse erro (reconectar). */
static esp_err_t check_result(obd_transport_t *t, elm327_t *elm, obd_poller_t *poller,
                              elm327_health_t *health, esp_err_t err, const char *what)
{
    bool link_up = t->ops->is_connected(t);
    switch (elm327_health_update(health, err, link_up)) {
    case ELM327_LINK_KEEP:
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) ESP_LOGW(TAG, "%s: %s", what, esp_err_to_name(err));
        return ESP_OK;
    case ELM327_LINK_REINIT:
        ESP_LOGW(TAG, "%s: %s; %d erros do ELM seguidos, reinicializando sem derrubar o link",
                 what, esp_err_to_name(err), ELM327_REINIT_AFTER);
        err = elm_init(t, elm);
        if (err != ESP_OK) return err;
        start_polling(poller, elm);
        return ESP_OK;
    case ELM327_LINK_RECONNECT:
    default:
        ESP_LOGW(TAG, "%s: %s (link %s)", what, esp_err_to_name(err), link_up ? "de pé" : "caído");
        return err != ESP_OK ? err : ESP_ERR_INVALID_STATE;
    }
}

/* Loop de leitura até um erro de transporte ou um pedido de re-pareamento. */
static esp_err_t run_session(obd_transport_t *t, elm327_t *elm)
{
    static obd_poller_t poller;
    elm327_health_t health;
    elm327_health_init(&health);
    start_polling(&poller, elm);
    uint32_t last_atrv = app_now_ms() - ATRV_PERIOD_MS;   /* primeira leitura já */
    bool atrv_warned = false;

    while (!s_repair) {
        uint32_t now = app_now_ms();
        if (now - last_atrv >= ATRV_PERIOD_MS) {
            last_atrv = now;
            float v = NAN;
            esp_err_t err = elm327_read_voltage(elm, &v);
            if (err == ESP_OK) {
                publish_battery(v);
            } else if (err == ESP_ERR_INVALID_RESPONSE && t->ops->is_connected(t)) {
                /* Resposta sem número com o link de pé (clone sem ATRV): segue sem tensão e não
                   conta como erro do ELM (senão um clone sem ATRV reinicializaria sempre). */
                publish_battery(NAN);
                if (!atrv_warned) ESP_LOGW(TAG, "ATRV sem resposta numérica; tensão desconhecida");
                atrv_warned = true;
            } else {
                err = check_result(t, elm, &poller, &health, err, "ATRV");
                if (err != ESP_OK) return err;
            }
        }

        uint8_t pid = obd_poller_next(&poller, now);
        if (pid == 0) {   /* não acontece: RPM e velocidade sempre entram na lista */
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        float value = 0;
        esp_err_t err = elm327_read_pid(elm, pid, &value);
        bool answered = err == ESP_OK || err == ESP_ERR_NOT_FOUND;   /* NOT_FOUND = NO DATA */
        char what[8];
        snprintf(what, sizeof(what), "PID %02X", pid);
        esp_err_t verdict = check_result(t, elm, &poller, &health, err, what);
        if (verdict != ESP_OK) return verdict;
        if (!answered) continue;
        now = app_now_ms();
        obd_poller_record(&poller, pid, value, err == ESP_OK, now);
        publish_sample(&poller, pid, value, err == ESP_OK, now);
        rec_autostart(pid, value, err == ESP_OK);
    }
    return ESP_OK;
}

/* Cria o transporte padrão do build (Kconfig) com o endereço do NVS, ou procurando pelo
   nome se não houver. ESP_ERR_NOT_SUPPORTED: o build não tem transporte. */
static esp_err_t create_transport(obd_transport_t **out, bool *have_mac)
{
    uint8_t mac[OBD_TRANSPORT_PEER_LEN];
    *have_mac = storage_get_elm_mac(mac) == ESP_OK;
    obd_transport_default_cfg_t cfg = {
        .name = CONFIG_OBD_ELM_BT_NAME,
        .pin = CONFIG_OBD_ELM_BT_PIN,
        .peer = *have_mac ? mac : NULL,
    };
    obd_transport_t *t = NULL;
    esp_err_t err = obd_transport_create_default(&cfg, &t);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NOT_SUPPORTED) ESP_LOGE(TAG, "obd_transport_create_default: %s", esp_err_to_name(err));
        return err;
    }
    if (*have_mac) {
        ESP_LOGI(TAG, "ELM salvo: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        ESP_LOGI(TAG, "sem ELM salvo: procurando \"%s\"", CONFIG_OBD_ELM_BT_NAME);
    }
    *out = t;
    return ESP_OK;
}

/* Primeira conexão por nome: guarda o endereço para as próximas irem direto. Transporte sem
   endereço (get_peer opcional) não guarda nada. */
static void remember_peer(obd_transport_t *t, bool *have_mac)
{
    uint8_t mac[OBD_TRANSPORT_PEER_LEN];
    esp_err_t err = obd_transport_get_peer(t, mac);
    if (err == ESP_ERR_NOT_SUPPORTED) return;
    if (err == ESP_OK) err = storage_set_elm_mac(mac);
    if (err == ESP_OK) {
        *have_mac = true;
        ESP_LOGI(TAG, "MAC do ELM salvo: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        ESP_LOGW(TAG, "não foi possível salvar o MAC do ELM: %s", esp_err_to_name(err));
    }
}

static void obd_task(void *arg)
{
    static elm327_t elm;
    obd_transport_t *t = NULL;
    bool have_mac = false;
    uint32_t backoff = BACKOFF_MIN_MS;

    for (;;) {
        if (s_repair) {
            s_repair = false;
            ulTaskNotifyTake(pdTRUE, 0);
            ESP_LOGI(TAG, "re-pareamento: esquecendo o MAC salvo");
            esp_err_t err = storage_erase_elm_mac();
            if (err != ESP_OK) ESP_LOGW(TAG, "storage_erase_elm_mac: %s", esp_err_to_name(err));
            /* Recriar zera também o MAC lembrado dentro do transporte. */
            if (t) t->ops->destroy(t);
            t = NULL;
            backoff = BACKOFF_MIN_MS;
        }
        if (!t) {
            esp_err_t err = create_transport(&t, &have_mac);
            if (err == ESP_ERR_NOT_SUPPORTED) break;   /* build sem transporte */
        }

        if (t) {
            set_state(APP_OBD_CONNECTING);
            esp_err_t err = t->ops->connect(t, CONNECT_TIMEOUT_MS);
            if (err == ESP_OK) err = elm_init(t, &elm);
            if (err == ESP_OK) {
                if (!have_mac) remember_peer(t, &have_mac);
                ESP_LOGI(TAG, "conectado: protocolo %s, %u PIDs", elm.protocol, count_supported(&elm));
                backoff = BACKOFF_MIN_MS;
                err = run_session(t, &elm);
                if (err != ESP_OK) ESP_LOGW(TAG, "sessão caiu (%s), reconectando", esp_err_to_name(err));
            }
            t->ops->disconnect(t);
        }

        set_state(APP_OBD_NO_ELM);
        if (s_repair) continue;
        ESP_LOGI(TAG, "nova tentativa em %lu ms", (unsigned long)backoff);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(backoff));
        backoff = backoff * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : backoff * 2;
    }

    ESP_LOGW(TAG, "sem transporte disponível neste build");
    set_state(APP_OBD_NO_TRANSPORT);
    s_task = NULL;   /* xTaskCreate já gravou o handle antes de a task rodar */
    vTaskDelete(NULL);
}

esp_err_t app_obd_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = APP_OBD_CONNECTING;   /* a task troca para NO_TRANSPORT se o build não tiver */
    s_status.battery_v = NAN;
    if (xTaskCreate(obd_task, "app_obd", OBD_TASK_STACK, NULL, OBD_TASK_PRIO, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
