/* Task da interface: lê o status do app_obd, roda o gear_engine, atualiza as telas, toca o
   bipe, trata os botões e decide o sono profundo. Única task que chama ui_show_* (LVGL). */
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "sdkconfig.h"
#include "app_obd.h"
#include "app_ui.h"
#include "board_api.h"
#include "gear_engine.h"
#include "obd_recorder.h"
#include "power_mgr.h"
#include "storage.h"
#include "ui.h"
#include "vehicle_profile.h"

#define UI_TASK_PRIO      4
#define UI_TASK_STACK     6144
#define UI_PERIOD_MS      50       /* 20 Hz */
#define DIAG_EVERY_TICKS  10       /* tela de diagnóstico a 2 Hz */
#define POWER_EVERY_TICKS 20       /* decisão de sono a cada 1 s */
#define WAKE_DECIDE_MS    30000    /* prazo para conectar depois de acordar pelo timer */
#define BTN_QUEUE_LEN     8
#define STACK_LOG_MS      60000

static const char *TAG = "app_ui";

typedef struct { board_button_t b; board_press_t p; } btn_evt_t;

static QueueHandle_t s_btn_q;
static const vehicle_profile_t *s_profile;
static gear_engine_t s_engine;   /* ~800 B: fora da pilha */

typedef struct {
    /* A tela visível é a do módulo ui (ui_screen_is_diag); aqui só a escolha automática. */
    bool auto_done;        /* já foi para a de marcha com dados, ou o usuário apertou B */
    bool muted;
    bool wake_pending;     /* acordou pelo timer e ainda não decidiu se fica acordado */
    float wake_max_rpm;    /* maior RPM visto desde que acordou pelo timer */
    bool stack_logged, rec_was_active;
    ui_alert_t alert;
    gear_state_t gs;
    bool have_gs;
    uint32_t last_rpm_t, last_spd_t;
} ui_ctx_t;

/* Roda na task do esp_timer: só posta o evento. */
static void on_button(board_button_t b, board_press_t p, void *arg)
{
    btn_evt_t ev = {b, p};
    xQueueSend(s_btn_q, &ev, 0);
}

/* Kconfig APP_PROFILE_ID, sobrescrito pelo perfil salvo no NVS; id desconhecido → padrão. */
static const vehicle_profile_t *pick_profile(void)
{
    const vehicle_profile_t *p = NULL;
    char id[32];
    if (storage_get_profile_id(id, sizeof(id)) == ESP_OK) {
        p = vp_find(id);
        if (!p) ESP_LOGW(TAG, "perfil \"%s\" do NVS não existe", id);
    }
    if (!p) p = vp_find(CONFIG_APP_PROFILE_ID);
    if (!p) {
        ESP_LOGW(TAG, "perfil \"%s\" não existe; usando o padrão", CONFIG_APP_PROFILE_ID);
        p = vp_default();
    }
    return p;
}

static void init_engine(void)
{
    float learned[VP_MAX_GEARS];
    uint8_t n = 0;
    esp_err_t err = storage_load_ratios(s_profile->id, learned, &n);
    bool use = err == ESP_OK && n == s_profile->gears;
    gear_engine_init(&s_engine, s_profile, use ? learned : NULL);
    ESP_LOGI(TAG, "perfil %s: %s", s_profile->id, use ? "relações aprendidas do NVS" : "aprendendo");
}

/* Dois bipes: subindo para "suba", descendo para "reduza". Bloqueia ~220 ms. */
static void play_tone(ui_tone_t tone)
{
    bool up = tone == UI_TONE_UP;
    if (board_buzzer_tone(up ? 1800 : 2600, 90) != ESP_OK) return;
    vTaskDelay(pdMS_TO_TICKS(130));
    board_buzzer_tone(up ? 2600 : 1600, 90);
}

static void handle_button(ui_ctx_t *c, const btn_evt_t *ev)
{
    bool lng = ev->p == BOARD_PRESS_LONG;
    switch (ev->b) {
    case BOARD_BTN_A:
        if (!lng) {
            c->muted = !c->muted;
            esp_err_t err = storage_set_mute(c->muted);
            if (err != ESP_OK) ESP_LOGW(TAG, "storage_set_mute: %s", esp_err_to_name(err));
            ESP_LOGI(TAG, "bipe %s", c->muted ? "mudo" : "ligado");
        } else {
            gear_engine_reset_learning(&s_engine);
            esp_err_t err = storage_erase_ratios(s_profile->id);
            if (err != ESP_OK) ESP_LOGW(TAG, "storage_erase_ratios: %s", esp_err_to_name(err));
            ESP_LOGI(TAG, "reaprendendo as marchas");
        }
        break;
    case BOARD_BTN_B:
        if (!lng) {
            ui_toggle_screen();
            c->auto_done = true;
        } else {
            ESP_LOGI(TAG, "re-parear o ELM");
            app_obd_request_repair();
        }
        break;
    default:
        break;
    }
}

/* Alimenta o gear_engine só com amostra nova (repetir a mesma amostra a 20 Hz contaminaria o
   aprendizado) ou quando ela envelheceu (para a tela cair em "sem dados"). */
static void step_engine(ui_ctx_t *c, const app_obd_status_t *st, uint32_t now)
{
    const obd_snapshot_t *s = &st->snap;
    if (!s->rpm.valid || !s->speed_kmh.valid) {
        c->gs = (gear_state_t){.stale = true};
        c->have_gs = false;
        return;
    }
    /* R34: stale e hold acompanham a taxa medida do ELM. */
    gear_engine_set_poll_cycle(&s_engine, s->fast_cycle_ms);
    uint32_t sample_ms = s->rpm.t_ms < s->speed_kmh.t_ms ? s->rpm.t_ms : s->speed_kmh.t_ms;
    bool fresh = !c->have_gs || s->rpm.t_ms != c->last_rpm_t || s->speed_kmh.t_ms != c->last_spd_t;
    bool stale = gear_sample_is_stale(now, sample_ms, gear_engine_stale_ms(&s_engine));
    if (!fresh && !stale) return;

    gear_input_t in = {
        .now_ms = now,
        .sample_ms = sample_ms,
        .rpm = s->rpm.value,
        .speed_kmh = s->speed_kmh.value,
        .load_pct = s->load_pct.valid ? s->load_pct.value : 0,
        .throttle_pct = s->throttle_pct.valid ? s->throttle_pct.value : NAN,
    };
    c->gs = gear_engine_update(&s_engine, &in);
    c->have_gs = true;
    c->last_rpm_t = s->rpm.t_ms;
    c->last_spd_t = s->speed_kmh.t_ms;

    float learned[VP_MAX_GEARS];
    if (gear_engine_take_learned(&s_engine, learned)) {
        esp_err_t err = storage_save_ratios(s_profile->id, learned, s_profile->gears);
        if (err == ESP_OK) ESP_LOGI(TAG, "%u marchas aprendidas e salvas", s_profile->gears);
        else ESP_LOGW(TAG, "storage_save_ratios: %s", esp_err_to_name(err));
    }
}

static const char *status_text(app_obd_state_t s)
{
    /* Sem acento nem reticências: as fontes embutidas do LVGL só têm ASCII. */
    switch (s) {
    case APP_OBD_NO_TRANSPORT: return "Sem transporte";
    case APP_OBD_CONNECTING: return "Conectando...";
    case APP_OBD_NO_ELM: return "Sem ELM";
    case APP_OBD_CONNECTED: return "Conectado";
    }
    return "-";
}

/* Valor só se conectado e com amostra recente (mesma regra de stale do gear_engine);
   senão NAN (nada da sessão anterior). */
static float live_value(const app_obd_status_t *st, const obd_value_t *v, uint32_t now)
{
    if (!st->connected || !v->valid || gear_sample_is_stale(now, v->t_ms, gear_engine_stale_ms(&s_engine))) return NAN;
    return v->value;
}

static void show_diag(const app_obd_status_t *st, uint32_t now)
{
    const obd_snapshot_t *s = &st->snap;
    ui_diag_t d = {
        .status = status_text(st->state),
        .protocol = st->protocol[0] ? st->protocol : "-",
        .n_supported = st->n_supported,
        .rate_hz = s->rate_hz,
        .rpm = live_value(st, &s->rpm, now),
        .speed_kmh = live_value(st, &s->speed_kmh, now),
        .battery_v = st->battery_v,
    };
    ui_show_diag(&d);
}

static void go_sleep(const char *why)
{
    ESP_LOGI(TAG, "dormindo: %s", why);
    if (rec_active()) rec_stop();
    board_deep_sleep(POWER_WAKE_MS);
}

static void check_power(ui_ctx_t *c, const app_obd_status_t *st, uint32_t now)
{
    if (c->wake_pending) {
        /* Decide uma vez: fica se a tensão ou algum RPM já mostram o motor ligado. Dorme cedo
           só com tensão < POWER_RUN_V e depois de algum pedido de RPM respondido (valor ou NO
           DATA): o ATRV sai antes do primeiro RPM, e na partida a tensão ainda está baixa.
           Sem isso, dorme no fim do prazo. O boot após o sono é a sessão de despertar, então
           o contador desde o boot basta. */
        float rpm = live_value(st, &st->snap.rpm, now);
        if (rpm > c->wake_max_rpm) c->wake_max_rpm = rpm;   /* NAN compara falso */
        bool stay = power_should_stay_awake_after_wake(st->battery_v, c->wake_max_rpm);
        bool can_judge = !isnan(st->battery_v) && st->rpm_answers > 0;
        if (!stay && !can_judge && now < WAKE_DECIDE_MS) return;
        c->wake_pending = false;
        if (stay) {
            ESP_LOGI(TAG, "acordou pelo timer: motor ligado, continua acordado");
        } else {
            go_sleep("acordou pelo timer e o motor não está ligado");
        }
        return;
    }
    uint32_t idle_ms = CONFIG_APP_POWER_IDLE_MS_OVERRIDE > 0 ? CONFIG_APP_POWER_IDLE_MS_OVERRIDE : POWER_IDLE_MS;
    power_inputs_t in = {
        .now_ms = now,
        .last_engine_running_ms = st->last_engine_running_ms,
        .last_elm_ok_ms = st->last_elm_ok_ms,
        .battery_v = st->battery_v,
    };
    if (power_should_sleep_after(&in, idle_ms)) go_sleep("motor parado e bateria sem carga");
}

static void ui_task(void *arg)
{
    ui_ctx_t c = {0};
    c.muted = storage_get_mute();
    c.wake_pending = (esp_sleep_get_wakeup_causes() & (1u << ESP_SLEEP_WAKEUP_TIMER)) != 0;
    ui_alert_init(&c.alert);
    c.gs.stale = true;
    if (c.wake_pending) ESP_LOGI(TAG, "acordou pelo timer: checando o motor");

    TickType_t wake = xTaskGetTickCount();
    for (uint32_t tick = 0;; tick++) {
        btn_evt_t ev;
        while (xQueueReceive(s_btn_q, &ev, 0) == pdTRUE) handle_button(&c, &ev);

        app_obd_status_t st;
        app_obd_get(&st);
        uint32_t now = app_now_ms();
        step_engine(&c, &st, now);

        /* Diagnóstico (Etapa 0) até haver dados, depois marcha, uma vez; B assume o controle.
           Se o toggle falhar (lock do LVGL), tenta de novo no próximo tick. */
        if (!c.auto_done) {
            bool have_data = st.connected && st.snap.rpm.valid && st.snap.speed_kmh.valid;
            if (ui_screen_is_diag() == have_data) ui_toggle_screen();
            if (have_data && !ui_screen_is_diag()) c.auto_done = true;
        }

        /* As duas telas sempre atualizadas (cada uma só redesenha se mudar): a visível nunca
           fica congelada com um conselho velho, seja qual for a tela. */
        ui_show_gear(&c.gs, st.connected);
        if (tick % DIAG_EVERY_TICKS == 0) show_diag(&st, now);

        /* Desconectado, um conselho de antes da queda não pode bipar. */
        shift_advice_t advice = st.connected ? c.gs.advice : SHIFT_NONE;
        ui_tone_t tone = ui_alert_update(&c.alert, advice, c.gs.gear, now, c.muted);
        if (tone != UI_TONE_NONE) play_tone(tone);

        if (tick % POWER_EVERY_TICKS == 0) check_power(&c, &st, now);

        /* Uso de stack (checklist no carro): uma vez aos 60 s e a cada "rec off". */
        bool rec_on = rec_active();
        if ((!c.stack_logged && now >= STACK_LOG_MS) || (c.rec_was_active && !rec_on)) {
            c.stack_logged = true;
            ESP_LOGI(TAG, "stack livre mínima: app_obd %lu B, app_ui %lu B",
                     (unsigned long)app_obd_stack_free(), (unsigned long)uxTaskGetStackHighWaterMark(NULL));
        }
        c.rec_was_active = rec_on;

        vTaskDelayUntil(&wake, pdMS_TO_TICKS(UI_PERIOD_MS));
    }
}

esp_err_t app_ui_start(void)
{
    s_profile = pick_profile();
    init_engine();
    s_btn_q = xQueueCreate(BTN_QUEUE_LEN, sizeof(btn_evt_t));
    if (!s_btn_q) return ESP_ERR_NO_MEM;
    esp_err_t err = board_buttons_register(on_button, NULL);
    if (err != ESP_OK) ESP_LOGW(TAG, "botões indisponíveis: %s", esp_err_to_name(err));
    if (xTaskCreate(ui_task, "app_ui", UI_TASK_STACK, NULL, UI_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
