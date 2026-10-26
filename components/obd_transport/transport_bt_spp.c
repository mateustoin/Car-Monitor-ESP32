/* Transporte Bluetooth clássico (SPP) para o ELM327. Base: exemplo
   bluedroid/classic_bt/bt_spp_initiator da IDF v6.1.

   Os callbacks do Bluedroid rodam na task BTC e conversam com connect() por um
   EventGroup. O contexto é estático (instância única): destroy não libera memória,
   então um callback atrasado nunca toca memória liberada. */
#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/stream_buffer.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "transport_bt_spp.h"

static const char *TAG = "bt_spp";

#define RX_BUF_SIZE        1024
#define MAX_UNNAMED        8      /* aparelhos sem nome no EIR, para ler o nome depois */
#define INQ_UNIT_MS        1280   /* unidade do inq_len da busca GAP */
#define INQ_LEN_MAX        8      /* 8 x 1,28 s ~ 10 s */
#define SPP_INIT_WAIT_MS   2000
#define CLOSE_WAIT_MS      1000
#define CONG_WAIT_MS       1000

#define BIT_FOUND     BIT0
#define BIT_CONNECTED BIT1
#define BIT_FAILED    BIT2
#define BIT_CLOSED    BIT3
#define BIT_UNCONG    BIT4
#define BIT_SPP_INIT  BIT5

#define BDA_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define BDA_ARG(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

typedef struct {
    obd_transport_t base;
    bool in_use;
    char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
    esp_bt_pin_code_t pin;
    uint8_t pin_len;
    bool has_mac;
    esp_bd_addr_t mac;

    volatile bool connecting;     /* connect() esperando: só então os eventos de busca valem */
    volatile bool connected;
    volatile bool congested;
    volatile uint32_t handle;
    esp_bd_addr_t target;         /* aparelho escolhido para a descoberta SPP */
    esp_bd_addr_t peer;
    bool has_peer;

    esp_bd_addr_t unnamed[MAX_UNNAMED];
    uint8_t unnamed_count, unnamed_next;

    EventGroupHandle_t events;
    StreamBufferHandle_t rx;
} bt_spp_t;

static bt_spp_t s_bt;
static bool s_stack_ready;
static StaticEventGroup_t s_events_buf;
static StaticStreamBuffer_t s_rx_struct;
static uint8_t s_rx_storage[RX_BUF_SIZE + 1];

/* ---------- busca do adaptador (task BTC) ---------- */

static bool name_matches(const uint8_t *name, size_t len)
{
    return len == strlen(s_bt.name) && memcmp(name, s_bt.name, len) == 0;
}

static void target_found(const esp_bd_addr_t bda)
{
    memcpy(s_bt.target, bda, sizeof(esp_bd_addr_t));
    xEventGroupSetBits(s_bt.events, BIT_FOUND);
    ESP_LOGI(TAG, "adaptador \"%s\" encontrado em " BDA_FMT, s_bt.name, BDA_ARG(bda));
    if (esp_spp_start_discovery(s_bt.target) != ESP_OK) {
        xEventGroupSetBits(s_bt.events, BIT_FAILED);
    }
}

/* Lê o nome remoto do próximo aparelho sem nome; sem mais candidatos, falha. */
static void read_next_unnamed(void)
{
    while (s_bt.unnamed_next < s_bt.unnamed_count) {
        if (esp_bt_gap_read_remote_name(s_bt.unnamed[s_bt.unnamed_next++]) == ESP_OK) return;
    }
    ESP_LOGW(TAG, "adaptador \"%s\" não encontrado na busca", s_bt.name);
    xEventGroupSetBits(s_bt.events, BIT_FAILED);
}

static void remember_unnamed(const esp_bd_addr_t bda)
{
    for (uint8_t i = 0; i < s_bt.unnamed_count; i++) {
        if (memcmp(s_bt.unnamed[i], bda, sizeof(esp_bd_addr_t)) == 0) return;
    }
    if (s_bt.unnamed_count < MAX_UNNAMED) {
        memcpy(s_bt.unnamed[s_bt.unnamed_count++], bda, sizeof(esp_bd_addr_t));
    }
}

static void on_disc_res(const struct disc_res_param *res)
{
    const uint8_t *name = NULL;
    uint8_t len = 0;
    for (int i = 0; i < res->num_prop && !name; i++) {
        const esp_bt_gap_dev_prop_t *p = &res->prop[i];
        if (p->type == ESP_BT_GAP_DEV_PROP_EIR) {
            name = esp_bt_gap_resolve_eir_data(p->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len);
            if (!name) name = esp_bt_gap_resolve_eir_data(p->val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &len);
        } else if (p->type == ESP_BT_GAP_DEV_PROP_BDNAME && p->len > 0) {
            name = p->val;
            len = (uint8_t)strnlen((const char *)p->val, (size_t)p->len);
        }
    }
    ESP_LOGI(TAG, "visto " BDA_FMT " nome \"%.*s\"", BDA_ARG(res->bda), name ? len : 0,
             name ? (const char *)name : "");
    if (!name) {
        remember_unnamed(res->bda);
    } else if (name_matches(name, len)) {
        esp_bt_gap_cancel_discovery();
        target_found(res->bda);
    }
}

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    bool searching = s_bt.connecting && !(xEventGroupGetBits(s_bt.events) & BIT_FOUND);
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT:
        if (searching) on_disc_res(&param->disc_res);
        break;
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        /* Busca terminou sem achar pelo EIR: tenta o nome remoto dos sem nome. */
        if (searching && param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) read_next_unnamed();
        break;
    case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
        if (!searching) break;
        if (param->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS &&
            name_matches(param->read_rmt_name.rmt_name,
                         strlen((const char *)param->read_rmt_name.rmt_name))) {
            target_found(param->read_rmt_name.bda);
        } else {
            read_next_unnamed();
        }
        break;
    case ESP_BT_GAP_PIN_REQ_EVT:
        /* Normalmente o PIN fixo já responde; isto cobre a pilha pedindo mesmo assim. */
        ESP_LOGI(TAG, "pedido de PIN de " BDA_FMT, BDA_ARG(param->pin_req.bda));
        esp_bt_gap_pin_reply(param->pin_req.bda, s_bt.pin_len > 0, s_bt.pin_len, s_bt.pin);
        break;
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(TAG, "SSP: confirmando %06" PRIu32, param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "pareado com \"%s\"", (const char *)param->auth_cmpl.device_name);
        } else {
            ESP_LOGE(TAG, "falha no pareamento, status %d (PIN errado?)", param->auth_cmpl.stat);
        }
        break;
    default:
        break;
    }
}

/* ---------- SPP (task BTC) ---------- */

static void spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event) {
    case ESP_SPP_INIT_EVT:
        if (param->init.status == ESP_SPP_SUCCESS) xEventGroupSetBits(s_bt.events, BIT_SPP_INIT);
        else ESP_LOGE(TAG, "SPP init falhou, status %d", param->init.status);
        break;
    case ESP_SPP_DISCOVERY_COMP_EVT:
        if (!s_bt.connecting) break;
        if (param->disc_comp.status == ESP_SPP_SUCCESS && param->disc_comp.scn_num > 0) {
            esp_spp_connect(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_MASTER, param->disc_comp.scn[0],
                            s_bt.target);
        } else {
            ESP_LOGE(TAG, "serviço SPP não encontrado, status %d", param->disc_comp.status);
            xEventGroupSetBits(s_bt.events, BIT_FAILED);
        }
        break;
    case ESP_SPP_CL_INIT_EVT:
        if (param->cl_init.status != ESP_SPP_SUCCESS && s_bt.connecting) {
            ESP_LOGE(TAG, "SPP connect falhou, status %d", param->cl_init.status);
            xEventGroupSetBits(s_bt.events, BIT_FAILED);
        }
        break;
    case ESP_SPP_OPEN_EVT:
        if (param->open.status != ESP_SPP_SUCCESS) {
            ESP_LOGE(TAG, "SPP open falhou, status %d", param->open.status);
            xEventGroupSetBits(s_bt.events, BIT_FAILED);
            break;
        }
        if (!s_bt.connecting) {   /* chegou depois do timeout do connect(): descarta */
            esp_spp_disconnect(param->open.handle);
            break;
        }
        xStreamBufferReset(s_bt.rx);
        s_bt.handle = param->open.handle;
        memcpy(s_bt.peer, param->open.rem_bda, sizeof(esp_bd_addr_t));
        s_bt.has_peer = true;
        s_bt.congested = false;
        s_bt.connected = true;
        ESP_LOGI(TAG, "connected " BDA_FMT " handle %" PRIu32, BDA_ARG(s_bt.peer), s_bt.handle);
        xEventGroupSetBits(s_bt.events, BIT_CONNECTED);
        break;
    case ESP_SPP_CLOSE_EVT:
        if (s_bt.connected && param->close.handle != s_bt.handle) break;
        ESP_LOGW(TAG, "desconectado (handle %" PRIu32 ", pelo remoto: %d)", param->close.handle,
                 param->close.async);
        s_bt.connected = false;
        xEventGroupSetBits(s_bt.events, BIT_CLOSED | BIT_UNCONG);
        break;
    case ESP_SPP_DATA_IND_EVT: {
        size_t sent = xStreamBufferSend(s_bt.rx, param->data_ind.data, param->data_ind.len, 0);
        if (sent < param->data_ind.len) {
            ESP_LOGW(TAG, "buffer de recepção cheio: %u bytes perdidos",
                     (unsigned)(param->data_ind.len - sent));
        }
        break;
    }
    case ESP_SPP_CONG_EVT:
        s_bt.congested = param->cong.cong;
        if (!param->cong.cong) xEventGroupSetBits(s_bt.events, BIT_UNCONG);
        break;
    case ESP_SPP_WRITE_EVT:
        if (param->write.status != ESP_SPP_SUCCESS) ESP_LOGW(TAG, "escrita falhou, status %d", param->write.status);
        s_bt.congested = param->write.cong;
        break;
    default:
        break;
    }
}

/* ---------- pilha BT (uma vez) ---------- */

#define TRY(x) do { esp_err_t e_ = (x); if (e_ != ESP_OK) { \
    ESP_LOGE(TAG, "%s: %s", #x, esp_err_to_name(e_)); return e_; } } while (0)

static esp_err_t stack_init(void)
{
    if (s_stack_ready) return ESP_OK;
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
        TRY(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        TRY(esp_bt_controller_init(&bt_cfg));
    }
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
        TRY(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    }
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        esp_bluedroid_config_t bd_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();   /* SSP ligado */
        TRY(esp_bluedroid_init_with_cfg(&bd_cfg));
    }
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) TRY(esp_bluedroid_enable());

    TRY(esp_bt_gap_register_callback(gap_cb));
    /* Sem tela/teclado no pareamento: SSP vira "just works". */
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    TRY(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(iocap)));
    TRY(esp_spp_register_callback(spp_cb));
    /* ERTM desligado: o modo básico do L2CAP é o que os clones do ELM327 aceitam. */
    esp_spp_cfg_t spp_cfg = {.mode = ESP_SPP_MODE_CB, .enable_l2cap_ertm = false, .tx_buffer_size = 0};
    TRY(esp_spp_enhanced_init(&spp_cfg));
    if (!(xEventGroupWaitBits(s_bt.events, BIT_SPP_INIT, pdFALSE, pdFALSE,
                              pdMS_TO_TICKS(SPP_INIT_WAIT_MS)) & BIT_SPP_INIT)) {
        ESP_LOGE(TAG, "SPP não inicializou");
        return ESP_ERR_TIMEOUT;
    }
    s_stack_ready = true;
    return ESP_OK;
}

/* ---------- operações ---------- */

static TickType_t ticks_left(TickType_t deadline)
{
    TickType_t now = xTaskGetTickCount();
    return (int32_t)(deadline - now) > 0 ? deadline - now : 0;
}

static esp_err_t bt_connect(obd_transport_t *t, uint32_t timeout_ms)
{
    (void)t;
    if (s_bt.connected) return ESP_OK;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    xEventGroupClearBits(s_bt.events, BIT_FOUND | BIT_CONNECTED | BIT_FAILED | BIT_CLOSED);
    s_bt.unnamed_count = s_bt.unnamed_next = 0;
    s_bt.connecting = true;

    esp_err_t err;
    if (s_bt.has_mac) {
        memcpy(s_bt.target, s_bt.mac, sizeof(esp_bd_addr_t));
        xEventGroupSetBits(s_bt.events, BIT_FOUND);
        err = esp_spp_start_discovery(s_bt.target);
    } else {
        uint32_t inq = timeout_ms / INQ_UNIT_MS;
        if (inq < 1) inq = 1;
        if (inq > INQ_LEN_MAX) inq = INQ_LEN_MAX;
        ESP_LOGI(TAG, "procurando \"%s\" (%" PRIu32 " x 1,28 s)", s_bt.name, inq);
        err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, (uint8_t)inq, 0);
    }
    if (err == ESP_OK) {
        EventBits_t bits = xEventGroupWaitBits(s_bt.events, BIT_CONNECTED | BIT_FAILED | BIT_CLOSED,
                                               pdFALSE, pdFALSE, ticks_left(deadline));
        if (bits & BIT_CONNECTED) err = ESP_OK;
        else if (bits & (BIT_FAILED | BIT_CLOSED)) err = ESP_FAIL;
        else err = ESP_ERR_TIMEOUT;
    }
    s_bt.connecting = false;
    if (s_bt.connected) err = ESP_OK;   /* OPEN chegou entre o fim da espera e a linha acima */
    if (err != ESP_OK) {
        esp_bt_gap_cancel_discovery();
        ESP_LOGW(TAG, "connect falhou: %s", esp_err_to_name(err));
    }
    return err;
}

/* Congestionado: espera o ESP_SPP_CONG_EVT liberar por até CONG_WAIT_MS. */
static esp_err_t bt_write(obd_transport_t *t, const char *data, size_t len)
{
    (void)t;
    if (!s_bt.connected) return ESP_ERR_INVALID_STATE;
    if (len == 0) return ESP_OK;
    if (s_bt.congested) {
        xEventGroupClearBits(s_bt.events, BIT_UNCONG);
        if (s_bt.congested) {
            xEventGroupWaitBits(s_bt.events, BIT_UNCONG, pdFALSE, pdFALSE, pdMS_TO_TICKS(CONG_WAIT_MS));
        }
        if (!s_bt.connected) return ESP_ERR_INVALID_STATE;
        if (s_bt.congested) return ESP_ERR_TIMEOUT;
    }
    /* esp_spp_write copia os dados antes de voltar. */
    return esp_spp_write(s_bt.handle, (int)len, (uint8_t *)data) == ESP_OK ? ESP_OK : ESP_FAIL;
}

static int bt_read(obd_transport_t *t, char *buf, size_t cap, uint32_t timeout_ms)
{
    (void)t;
    if (!s_bt.connected) return -1;
    size_t n = xStreamBufferReceive(s_bt.rx, buf, cap, pdMS_TO_TICKS(timeout_ms));
    if (n == 0 && !s_bt.connected) return -1;
    return (int)n;
}

static void bt_disconnect(obd_transport_t *t)
{
    (void)t;
    if (!s_bt.connected) return;
    xEventGroupClearBits(s_bt.events, BIT_CLOSED);
    if (esp_spp_disconnect(s_bt.handle) == ESP_OK) {
        xEventGroupWaitBits(s_bt.events, BIT_CLOSED, pdFALSE, pdFALSE, pdMS_TO_TICKS(CLOSE_WAIT_MS));
    }
    s_bt.connected = false;
}

static bool bt_is_connected(obd_transport_t *t)
{
    (void)t;
    return s_bt.connected;
}

/* Desconecta e libera a instância; a pilha BT e os objetos estáticos ficam. */
static void bt_destroy(obd_transport_t *t)
{
    bt_disconnect(t);
    s_bt.in_use = false;
}

static const obd_transport_ops_t bt_ops = {
    .connect = bt_connect, .write = bt_write, .read = bt_read,
    .disconnect = bt_disconnect, .is_connected = bt_is_connected, .destroy = bt_destroy,
};

esp_err_t transport_bt_spp_create(const transport_bt_spp_config_t *cfg, obd_transport_t **out)
{
    if (!cfg || !out) return ESP_ERR_INVALID_ARG;
    if (!cfg->mac && (!cfg->name || !cfg->name[0])) return ESP_ERR_INVALID_ARG;
    size_t name_len = cfg->name ? strlen(cfg->name) : 0;
    size_t pin_len = cfg->pin ? strlen(cfg->pin) : 0;
    if (name_len > ESP_BT_GAP_MAX_BDNAME_LEN || pin_len > ESP_BT_PIN_CODE_LEN) return ESP_ERR_INVALID_ARG;
    if (s_bt.in_use) return ESP_ERR_INVALID_STATE;

    if (!s_bt.events) {
        s_bt.events = xEventGroupCreateStatic(&s_events_buf);
        s_bt.rx = xStreamBufferCreateStatic(RX_BUF_SIZE, 1, s_rx_storage, &s_rx_struct);
    }
    esp_err_t err = stack_init();
    if (err != ESP_OK) return err;

    memcpy(s_bt.name, cfg->name ? cfg->name : "", name_len + 1);
    memset(s_bt.pin, 0, sizeof(s_bt.pin));
    memcpy(s_bt.pin, cfg->pin ? cfg->pin : "", pin_len);
    s_bt.pin_len = (uint8_t)pin_len;
    s_bt.has_mac = cfg->mac != NULL;
    if (cfg->mac) memcpy(s_bt.mac, cfg->mac, sizeof(esp_bd_addr_t));
    if (pin_len > 0) TRY(esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, s_bt.pin_len, s_bt.pin));
    else TRY(esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 0, s_bt.pin));

    s_bt.connected = false;
    s_bt.base.ops = &bt_ops;
    s_bt.base.ctx = &s_bt;
    s_bt.in_use = true;
    *out = &s_bt.base;
    return ESP_OK;
}

esp_err_t transport_bt_spp_get_peer(obd_transport_t *t, uint8_t mac[6])
{
    if (!t || t->ctx != &s_bt || !mac) return ESP_ERR_INVALID_ARG;
    if (!s_bt.has_peer) return ESP_ERR_INVALID_STATE;
    memcpy(mac, s_bt.peer, sizeof(esp_bd_addr_t));
    return ESP_OK;
}
