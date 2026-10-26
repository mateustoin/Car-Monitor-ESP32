/* BSP do M5StickC Plus2: LCD ST7789V2 (esp_lcd + esp_lvgl_port), buzzer (LEDC),
   3 botões (varredura por esp_timer), trava de energia (HOLD) e sono profundo. */
#include <stdbool.h>
#include <stdint.h>
#include "board_api.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "sdkconfig.h"

static const char *TAG = "bsp_plus2";

/* Pinos vêm do Kconfig ("M5StickC Plus2"); defaults de memória, conferir no esquemático M5Stack. */
#define PIN_LCD_MOSI CONFIG_BSP_PLUS2_PIN_LCD_MOSI
#define PIN_LCD_SCLK CONFIG_BSP_PLUS2_PIN_LCD_SCLK
#define PIN_LCD_DC   CONFIG_BSP_PLUS2_PIN_LCD_DC
#define PIN_LCD_RST  CONFIG_BSP_PLUS2_PIN_LCD_RST
#define PIN_LCD_CS   CONFIG_BSP_PLUS2_PIN_LCD_CS
#define PIN_LCD_BL   CONFIG_BSP_PLUS2_PIN_LCD_BL
#define PIN_BUZZER   CONFIG_BSP_PLUS2_PIN_BUZZER
#define PIN_BTN_A    CONFIG_BSP_PLUS2_PIN_BTN_A    /* ativo baixo; precisa ser RTC (ext0) */
#define PIN_BTN_B    CONFIG_BSP_PLUS2_PIN_BTN_B    /* ativo baixo */
#define PIN_BTN_PWR  CONFIG_BSP_PLUS2_PIN_BTN_PWR  /* ativo baixo */
#define PIN_HOLD     CONFIG_BSP_PLUS2_PIN_HOLD     /* alto mantém a placa ligada; precisa ser RTC (hold no sono) */

/* LCD 135x240 em paisagem: conferir no esquemático M5Stack. */
#define LCD_HOST       SPI2_HOST
#define LCD_PCLK_HZ    (20 * 1000 * 1000)   /* conservador (pinos pela matriz GPIO); a M5Stack usa até 40 MHz */
#ifdef CONFIG_BSP_PLUS2_LCD_SWAP_XY
#define LCD_H_RES 240
#define LCD_V_RES 135
#else
#define LCD_H_RES 135
#define LCD_V_RES 240
#endif
#define LCD_BUF_LINES 20

/* Opções bool do Kconfig ficam indefinidas quando desligadas. */
#ifdef CONFIG_BSP_PLUS2_LCD_INVERT
#define LCD_INVERT true
#else
#define LCD_INVERT false
#endif
#ifdef CONFIG_BSP_PLUS2_LCD_SWAP_XY
#define LCD_SWAP_XY true
#else
#define LCD_SWAP_XY false
#endif
#ifdef CONFIG_BSP_PLUS2_LCD_MIRROR_X
#define LCD_MIRROR_X true
#else
#define LCD_MIRROR_X false
#endif
#ifdef CONFIG_BSP_PLUS2_LCD_MIRROR_Y
#define LCD_MIRROR_Y true
#else
#define LCD_MIRROR_Y false
#endif

#define BUZZER_MODE    LEDC_LOW_SPEED_MODE
#define BUZZER_TIMER   LEDC_TIMER_0
#define BUZZER_CHANNEL LEDC_CHANNEL_0
#define BUZZER_RES     LEDC_TIMER_10_BIT
#define BUZZER_DUTY    (1u << ((unsigned)BUZZER_RES - 1))   /* 50% */

#define BTN_POLL_MS     20
#define BTN_DEBOUNCE_N  2                 /* leituras iguais seguidas = 40 ms */

static lv_display_t *s_disp;
static esp_timer_handle_t s_buzzer_off;
static esp_timer_handle_t s_btn_timer;
static board_button_cb_t s_btn_cb;
static void *s_btn_arg;
static bool s_inited;

typedef struct {
    gpio_num_t pin;
    bool stable;          /* pressionado (estado já filtrado) */
    bool raw_last;
    uint8_t same_count;
    uint32_t pressed_at_ms;
    bool long_sent;
} btn_state_t;

static btn_state_t s_btns[3] = {
    [BOARD_BTN_A]   = {.pin = PIN_BTN_A},
    [BOARD_BTN_B]   = {.pin = PIN_BTN_B},
    [BOARD_BTN_PWR] = {.pin = PIN_BTN_PWR},
};

static esp_err_t power_hold_init(void)
{
    /* Ao acordar do sono profundo o pino ainda está travado: configura alto antes de soltar. */
    gpio_config_t cfg = {.pin_bit_mask = 1ULL << PIN_HOLD, .mode = GPIO_MODE_OUTPUT};
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;
    gpio_set_level(PIN_HOLD, 1);
    return gpio_hold_dis(PIN_HOLD);
}

static esp_err_t lcd_init(void)
{
    gpio_config_t bl = {.pin_bit_mask = 1ULL << PIN_LCD_BL, .mode = GPIO_MODE_OUTPUT};
    esp_err_t err = gpio_config(&bl);
    if (err != ESP_OK) return err;
    gpio_set_level(PIN_LCD_BL, 0);

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_BUF_LINES * sizeof(uint16_t),
    };
    err = spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return err;

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = LCD_PCLK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io);
    if (err != ESP_OK) return err;

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
#ifdef CONFIG_BSP_PLUS2_LCD_BGR
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7789(io, &panel_cfg, &panel);
    if (err != ESP_OK) return err;
    err = esp_lcd_panel_reset(panel);
    if (err != ESP_OK) return err;
    err = esp_lcd_panel_init(panel);
    if (err != ESP_OK) return err;
    err = esp_lcd_panel_invert_color(panel, LCD_INVERT);
    if (err != ESP_OK) return err;
    /* O painel tem 240x320 de RAM; a janela visível começa no gap (conferir no esquemático M5Stack). */
    err = esp_lcd_panel_set_gap(panel, CONFIG_BSP_PLUS2_LCD_GAP_X, CONFIG_BSP_PLUS2_LCD_GAP_Y);
    if (err != ESP_OK) return err;

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    err = lvgl_port_init(&port_cfg);
    if (err != ESP_OK) return err;

    /* swap_xy/mirror são aplicados pelo esp_lvgl_port no painel ao adicionar o display. */
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = LCD_H_RES * LCD_BUF_LINES,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = LCD_SWAP_XY,
            .mirror_x = LCD_MIRROR_X,
            .mirror_y = LCD_MIRROR_Y,
        },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,   /* SPI manda RGB565 big-endian */
        },
    };
    s_disp = lvgl_port_add_disp(&disp_cfg);
    if (s_disp == NULL) return ESP_FAIL;

    err = esp_lcd_panel_disp_on_off(panel, true);
    if (err != ESP_OK) return err;
    gpio_set_level(PIN_LCD_BL, 1);
    return ESP_OK;
}

static void buzzer_off_cb(void *arg)
{
    (void)arg;
    ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0);
    ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
}

static esp_err_t buzzer_init(void)
{
    ledc_timer_config_t tcfg = {
        .speed_mode = BUZZER_MODE,
        .duty_resolution = BUZZER_RES,
        .timer_num = BUZZER_TIMER,
        .freq_hz = 2000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&tcfg);
    if (err != ESP_OK) return err;
    ledc_channel_config_t ccfg = {
        .gpio_num = PIN_BUZZER,
        .speed_mode = BUZZER_MODE,
        .channel = BUZZER_CHANNEL,
        .timer_sel = BUZZER_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&ccfg);
    if (err != ESP_OK) return err;
    esp_timer_create_args_t targs = {.callback = buzzer_off_cb, .name = "buzzer_off"};
    return esp_timer_create(&targs, &s_buzzer_off);
}

/* Varredura a cada 20 ms: filtra o ruído (2 leituras iguais), emite LONGO ao atingir
   2000 ms segurando e CURTO ao soltar antes disso. */
static void btn_poll_cb(void *arg)
{
    (void)arg;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    for (int i = 0; i < 3; i++) {
        btn_state_t *b = &s_btns[i];
        bool raw = gpio_get_level(b->pin) == 0;
        if (raw != b->raw_last) {
            b->raw_last = raw;
            b->same_count = 1;
            continue;
        }
        if (b->same_count < BTN_DEBOUNCE_N) b->same_count++;
        if (b->same_count < BTN_DEBOUNCE_N || raw == b->stable) {
            if (b->stable && !b->long_sent && now_ms - b->pressed_at_ms >= BOARD_LONG_PRESS_MS) {
                b->long_sent = true;
                if (s_btn_cb) s_btn_cb((board_button_t)i, BOARD_PRESS_LONG, s_btn_arg);
            }
            continue;
        }
        b->stable = raw;
        if (raw) {
            b->pressed_at_ms = now_ms;
            b->long_sent = false;
        } else if (!b->long_sent && s_btn_cb) {
            s_btn_cb((board_button_t)i, BOARD_PRESS_SHORT, s_btn_arg);
        }
    }
}

static esp_err_t buttons_init(void)
{
    /* GPIO34-39 não têm pull interno: os botões dependem dos pull-ups da placa. */
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_BTN_A) | (1ULL << PIN_BTN_B) | (1ULL << PIN_BTN_PWR),
        .mode = GPIO_MODE_INPUT,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;
    for (int i = 0; i < 3; i++) {
        /* Botão já seguro no boot (ex.: acordou por BTN_A) não gera evento até soltar. */
        bool raw = gpio_get_level(s_btns[i].pin) == 0;
        s_btns[i].raw_last = raw;
        s_btns[i].stable = raw;
        s_btns[i].same_count = BTN_DEBOUNCE_N;
        s_btns[i].long_sent = raw;
    }
    esp_timer_create_args_t targs = {.callback = btn_poll_cb, .name = "btn_poll"};
    err = esp_timer_create(&targs, &s_btn_timer);
    if (err != ESP_OK) return err;
    return esp_timer_start_periodic(s_btn_timer, BTN_POLL_MS * 1000);
}

esp_err_t board_init(void)
{
    if (s_inited) return ESP_OK;
    esp_err_t err = power_hold_init();
    if (err == ESP_OK) err = buzzer_init();
    if (err == ESP_OK) err = buttons_init();
    if (err == ESP_OK) err = lcd_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "falha na inicialização da placa: %s", esp_err_to_name(err));
        return err;
    }
    s_inited = true;
    ESP_LOGI(TAG, "M5StickC Plus2 pronto (%dx%d)", LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}

lv_display_t *board_display(void)
{
    return s_disp;
}

bool board_lvgl_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void board_lvgl_unlock(void)
{
    lvgl_port_unlock();
}

esp_err_t board_buzzer_tone(uint16_t freq_hz, uint16_t ms)
{
    if (!s_buzzer_off) return ESP_ERR_INVALID_STATE;
    if (freq_hz == 0 || ms == 0) return ESP_ERR_INVALID_ARG;
    esp_timer_stop(s_buzzer_off);   /* tom novo substitui o anterior */
    esp_err_t err = ledc_set_freq(BUZZER_MODE, BUZZER_TIMER, freq_hz);
    if (err != ESP_OK) return err;
    ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, BUZZER_DUTY);
    ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
    return esp_timer_start_once(s_buzzer_off, (uint64_t)ms * 1000);
}

esp_err_t board_buttons_register(board_button_cb_t cb, void *arg)
{
    s_btn_arg = arg;
    s_btn_cb = cb;
    return ESP_OK;
}

void board_deep_sleep(uint32_t wake_after_ms)
{
    ESP_LOGI(TAG, "sono profundo (timer %lu ms ou BTN_A)", (unsigned long)wake_after_ms);
    if (s_btn_timer) esp_timer_stop(s_btn_timer);
    if (s_buzzer_off) {
        esp_timer_stop(s_buzzer_off);
        buzzer_off_cb(NULL);   /* não dorme com o buzzer tocando */
    }
    gpio_set_level(PIN_LCD_BL, 0);
    esp_err_t err;
    if (wake_after_ms > 0) {
        err = esp_sleep_enable_timer_wakeup((uint64_t)wake_after_ms * 1000);
        if (err != ESP_OK) ESP_LOGE(TAG, "timer de despertar falhou: %s", esp_err_to_name(err));
    }
    err = esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_A, 0);
    if (err != ESP_OK) ESP_LOGE(TAG, "ext0 em BTN_A falhou (pino precisa ser RTC): %s", esp_err_to_name(err));
    /* HOLD é pino RTC: gpio_hold_en mantém o nível alto durante o sono profundo. */
    gpio_set_level(PIN_HOLD, 1);
    err = gpio_hold_en(PIN_HOLD);
    if (err != ESP_OK) ESP_LOGE(TAG, "hold do pino HOLD falhou (pino precisa ser RTC): %s", esp_err_to_name(err));
    esp_deep_sleep_start();
}
