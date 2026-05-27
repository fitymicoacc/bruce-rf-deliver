/**
 * ESP32-C3 Mini-Bruce: BLE GATT + CC1101 RX/TX
 *
 * Same wire protocol as Bruce-RF firmware. Accepts START_LISTEN, STOP,
 * PLAY, PING, SCAN_LIST, SCAN_RANGES via BLE CMD characteristic.
 * Sends decoded signals (0x80) and raw timings (0x81) via SIGNAL notify.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_sleep.h"

#include "board_config.h"
#include "cc1101_driver.h"
#include "RCSwitch.h"
#include "ble_rf_mini.h"

static const char *TAG = "MAIN";

/* LED on GPIO8, active LOW */
#define LED_PIN        GPIO_NUM_8
#define LED_ON()       gpio_set_level(LED_PIN, 0)
#define LED_OFF()      gpio_set_level(LED_PIN, 1)

/* BOOT button = GPIO9, mirrored to GPIO0 for deep sleep wakeup */
#define BOOT_PIN       GPIO_NUM_9
#define WAKE_PIN       GPIO_NUM_0
#define SLEEP_TIMEOUT_MS   60000
#define LONG_PRESS_MS      2000

static volatile uint8_t current_state = STATE_IDLE;
static volatile float   current_freq  = 0;
static volatile uint32_t last_connect_activity = 0;
static volatile bool led_suppress = false;

/* Dedup */
static uint8_t  last_rx_bits = 0;
static uint32_t last_rx_time = 0;
#define DEDUP_MS 1000

/* Scan state */
static float    scan_freqs[MAX_SCAN_FREQS];
static uint8_t  scan_freq_count = 0;
static uint8_t  scan_idx = 0;
static uint16_t scan_dwell_ms = 200;
static uint32_t scan_last_hop = 0;
static bool     scan_active = false;

/* RCSwitch protocol table (same as liblil, 23 entries) */
typedef struct {
    uint8_t  id;
    uint16_t T;
    uint8_t  sync_h, sync_l;
    uint8_t  zero_h, zero_l;
    uint8_t  one_h,  one_l;
    uint8_t  inverted;
} rc_proto_t;

static const rc_proto_t rc_protos[] = {
    { 1,  350,  1, 31,  1, 3,  3, 1, 0},
    { 2,  650,  1, 10,  1, 2,  2, 1, 0},
    { 3,  100, 30, 71,  4,11,  9, 6, 0},
    { 4,  380,  1,  6,  1, 3,  3, 1, 0},
    { 5,  500,  6, 14,  1, 2,  2, 1, 0},
    { 6,  450, 23,  1,  1, 2,  2, 1, 1},
    { 7,  150,  2, 62,  1, 6,  6, 1, 0},
    { 8,  200,  3,130,  7,16,  3,16, 0},
    { 9,  200,130,  7, 16, 7, 16, 3, 1},
    {10,  365, 18,  1,  3, 1,  1, 3, 1},
    {11,  270, 36,  1,  1, 2,  2, 1, 1},
    {12,  320, 36,  1,  1, 2,  2, 1, 1},
    {13,  100,  3,100,  3, 8,  8, 3, 0},
    {14,  500,  1, 14,  1, 3,  3, 1, 0},
    {15,  415,  1, 30,  1, 3,  4, 1, 0},
    {16,  250, 20, 10,  1, 1,  3, 1, 0},
    {17,   80,  3, 25,  3,13, 11, 5, 0},
    {18,   82,  2, 65,  3, 5,  7, 1, 0},
    {19,  560, 16,  8,  1, 1,  1, 3, 0},
    {20,  250,  1,  3,  2, 1,  1, 2, 0},
    {21,  330,  1, 34,  2, 1,  1, 2, 0},
    {22,  700,  1, 36,  2, 1,  1, 2, 0},
    {23,  400,  0, 10,  2, 1,  1, 2, 0},
};
#define RC_PROTO_COUNT (sizeof(rc_protos)/sizeof(rc_protos[0]))

static const rc_proto_t* find_proto(uint8_t id) {
    for (int i = 0; i < (int)RC_PROTO_COUNT; i++)
        if (rc_protos[i].id == id) return &rc_protos[i];
    return NULL;
}

/* ===== RCSwitch-style decoder on raw timings ===== */

static unsigned int abs_diff(unsigned int a, unsigned int b) {
    return a > b ? a - b : b - a;
}

static bool try_decode_protocol(const rc_proto_t *p, const unsigned int *timings,
                                int change_count, uint64_t *out_code, uint8_t *out_bits,
                                uint16_t *out_T)
{
    int first_data = p->inverted ? 2 : 1;
    int data_bits = (change_count - first_data) / 2;
    if (data_bits < 4 || data_bits > 64) return false;

    unsigned int sync_long = p->sync_h > p->sync_l ? p->sync_h : p->sync_l;
    if (sync_long == 0) return false;

    unsigned int delay = timings[0] / sync_long;
    if (delay == 0) return false;

    if (abs_diff(delay, p->T) > p->T * 30 / 100) return false;

    unsigned int base_delta = delay * 60 / 100;
    uint64_t code = 0;

    for (int i = 0; i < data_bits; i++) {
        unsigned int t1 = timings[first_data + i * 2];
        unsigned int t2 = timings[first_data + i * 2 + 1];

        unsigned int d1z = abs_diff(t1, delay * p->zero_h);
        unsigned int d2z = abs_diff(t2, delay * p->zero_l);
        bool is_zero = (d1z < (p->zero_h ? p->zero_h : 1) * base_delta) &&
                       (d2z < (p->zero_l ? p->zero_l : 1) * base_delta);

        unsigned int d1o = abs_diff(t1, delay * p->one_h);
        unsigned int d2o = abs_diff(t2, delay * p->one_l);
        bool is_one = (d1o < (p->one_h ? p->one_h : 1) * base_delta) &&
                      (d2o < (p->one_l ? p->one_l : 1) * base_delta);

        code <<= 1;
        if (is_one && !is_zero) {
            code |= 1;
        } else if (is_zero && !is_one) {
            /* bit 0 */
        } else if (is_one && is_zero) {
            if ((d1o + d2o) < (d1z + d2z)) code |= 1;
        } else {
            return false;
        }
    }

    if (code == 0) return false;

    *out_code = code;
    *out_bits = (uint8_t)data_bits;
    *out_T = (uint16_t)delay;
    return true;
}

/* ===== TX ===== */

static void raw_ook_tx(const uint8_t *bits_buf, int num_bits, uint16_t period_us)
{
    gpio_num_t gdo0 = CC1101_PIN_GDO0;
    gpio_isr_handler_remove(gdo0);
    gpio_set_direction(gdo0, GPIO_MODE_OUTPUT);
    gpio_set_level(gdo0, 0);

    cc1101_set_idle_state();
    esp_rom_delay_us(100);
    cc1101_write_config_reg_raw(0x10, 0x67);
    cc1101_write_config_reg_raw(0x11, 0x83);
    cc1101_set_tx_state_raw();
    esp_rom_delay_us(800);

    for (int i = 0; i < num_bits; i++) {
        gpio_set_level(gdo0, bits_buf[i] ? 1 : 0);
        esp_rom_delay_us(period_us);
    }

    gpio_set_level(gdo0, 0);
    esp_rom_delay_us(100);
    cc1101_set_idle_state();

    cc1101_write_config_reg_raw(0x10, 0x17);
    cc1101_write_config_reg_raw(0x11, 0x32);
    gpio_set_direction(gdo0, GPIO_MODE_INPUT);
}

static int ook_encode(const rc_proto_t *p, uint64_t code, int num_bits, uint8_t *buf)
{
    int pos = 0;
    uint8_t on  = p->inverted ? 0 : 1;
    uint8_t off = p->inverted ? 1 : 0;

    for (int bit = num_bits - 1; bit >= 0; bit--) {
        if ((code >> bit) & 1) {
            for (int i = 0; i < p->one_h; i++) buf[pos++] = on;
            for (int i = 0; i < p->one_l; i++) buf[pos++] = off;
        } else {
            for (int i = 0; i < p->zero_h; i++) buf[pos++] = on;
            for (int i = 0; i < p->zero_l; i++) buf[pos++] = off;
        }
    }
    for (int i = 0; i < p->sync_h; i++) buf[pos++] = on;
    for (int i = 0; i < p->sync_l; i++) buf[pos++] = off;
    return pos;
}

static void do_play(const ble_rf_cmd_t *cmd)
{
    const rc_proto_t *p = find_proto(cmd->protocol);
    if (!p) {
        ESP_LOGW(TAG, "Unknown protocol %d", cmd->protocol);
        ble_rf_mini_notify_status(STATE_ERROR, ERR_TX_FAIL);
        return;
    }

    uint8_t prev_state = current_state;
    current_state = STATE_TRANSMITTING;
    ble_rf_mini_notify_status(STATE_TRANSMITTING, ERR_NONE);

    uint8_t frame[768];
    int frame_len = ook_encode(p, cmd->key, cmd->bits, frame);
    uint8_t burst[3072];
    int burst_len = 0;
    for (int r = 0; r < 10; r++) {
        memcpy(&burst[burst_len], frame, frame_len);
        burst_len += frame_len;
    }

    ESP_LOGI(TAG, "TX: P%d key=0x%llX %dbit T=%d freq=%.2f",
             cmd->protocol, (unsigned long long)cmd->key,
             cmd->bits, p->T, cmd->freq);

    raw_ook_tx(burst, burst_len, p->T);

    if (prev_state == STATE_LISTENING || prev_state == STATE_SCANNING) {
        cc1101_start_rx_capture();
        cc1101_set_rx_state();
        current_state = prev_state;
        ble_rf_mini_notify_status(prev_state, ERR_NONE);
    } else {
        current_state = STATE_IDLE;
        ble_rf_mini_notify_status(STATE_IDLE, ERR_NONE);
    }
}

/* ===== Listening ===== */

static void start_listening(float freq)
{
    scan_active = false;
    cc1101_set_idle_state();
    esp_rom_delay_us(100);
    cc1101_start_rx_capture();
    cc1101_set_rx_state();
    vTaskDelay(pdMS_TO_TICKS(10));
    cc1101_clear_pulses();

    current_freq = freq;
    current_state = STATE_LISTENING;
    ble_rf_mini_notify_status(STATE_LISTENING, ERR_NONE);
    ESP_LOGI(TAG, "Listening on %.2f MHz", freq);
}

static void stop_listening(void)
{
    scan_active = false;
    cc1101_set_idle_state();
    current_state = STATE_IDLE;
    ble_rf_mini_notify_status(STATE_IDLE, ERR_NONE);
    ESP_LOGI(TAG, "Stopped");
}

/* ===== Scan ===== */

static void hop_to_freq(float freq)
{
    cc1101_set_idle_state();
    cc1101_set_carrier_freq(freq > 800 ? CFREQ_868 : freq > 400 ? CFREQ_433 : CFREQ_315);
    cc1101_start_rx_capture();
    cc1101_set_rx_state();
    cc1101_clear_pulses();
    current_freq = freq;
}

static void start_scan_list(const ble_rf_cmd_t *cmd)
{
    scan_active = false;
    if (cmd->scan_count == 0) return;

    cc1101_set_idle_state();
    esp_rom_delay_us(100);

    scan_freq_count = cmd->scan_count;
    for (int i = 0; i < cmd->scan_count; i++)
        scan_freqs[i] = cmd->scan_freqs[i];
    scan_dwell_ms = cmd->dwell_ms < 50 ? 50 : cmd->dwell_ms;
    scan_idx = 0;
    scan_active = true;
    scan_last_hop = 0;

    hop_to_freq(scan_freqs[0]);
    current_state = STATE_SCANNING;
    ble_rf_mini_notify_status(STATE_SCANNING, ERR_NONE);
    ESP_LOGI(TAG, "Scan list: %d freqs, dwell=%dms", scan_freq_count, scan_dwell_ms);
}

static void start_scan_ranges(const ble_rf_cmd_t *cmd)
{
    scan_active = false;
    if (cmd->scan_count == 0) return;

    cc1101_set_idle_state();
    esp_rom_delay_us(100);

    scan_freq_count = 0;
    for (int r = 0; r < cmd->scan_count && scan_freq_count < MAX_SCAN_FREQS; r++) {
        float f = cmd->scan_ranges[r].start;
        while (f <= cmd->scan_ranges[r].end + 0.001f && scan_freq_count < MAX_SCAN_FREQS) {
            scan_freqs[scan_freq_count++] = f;
            f += cmd->scan_ranges[r].step;
        }
    }

    if (scan_freq_count == 0) return;

    scan_dwell_ms = cmd->dwell_ms < 50 ? 50 : cmd->dwell_ms;
    scan_idx = 0;
    scan_active = true;
    scan_last_hop = 0;

    hop_to_freq(scan_freqs[0]);
    current_state = STATE_SCANNING;
    ble_rf_mini_notify_status(STATE_SCANNING, ERR_NONE);
    ESP_LOGI(TAG, "Scan ranges: %d freqs, dwell=%dms", scan_freq_count, scan_dwell_ms);
}

static void poll_scan(void)
{
    if (!scan_active || scan_freq_count == 0) return;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (scan_last_hop != 0 && (now - scan_last_hop) < scan_dwell_ms) return;

    scan_idx = (scan_idx + 1) % scan_freq_count;
    scan_last_hop = now;
    hop_to_freq(scan_freqs[scan_idx]);
}

/* ===== BLE command handler ===== */

static void on_ble_cmd(const ble_rf_cmd_t *cmd)
{
    switch (cmd->cmd) {
    case CMD_START_LISTEN:
        start_listening(cmd->freq);
        break;
    case CMD_STOP:
        stop_listening();
        break;
    case CMD_PLAY:
        do_play(cmd);
        break;
    case CMD_PING:
        ble_rf_mini_notify_status(current_state, ERR_NONE);
        ESP_LOGI(TAG, "PONG (state=%d)", current_state);
        break;
    case CMD_SCAN_LIST:
        start_scan_list(cmd);
        break;
    case CMD_SCAN_RANGES:
        start_scan_ranges(cmd);
        break;
    default:
        ESP_LOGW(TAG, "Unhandled cmd 0x%02X", cmd->cmd);
        break;
    }
}

/* ===== RX processing ===== */

#define RX_SEPARATION  4300
#define RX_MAX_TIM     512

static unsigned int rx_timings[RX_MAX_TIM];
static int rx_count = 0;
static bool rx_got_sync = false;

static void handle_frame(void)
{
    if (rx_count < 8) return;

    /* Try to decode against all protocols */
    int best_proto_idx = -1;
    uint64_t best_code = 0;
    uint8_t  best_bits = 0;
    uint16_t best_T = 0;

    for (int p = 0; p < (int)RC_PROTO_COUNT; p++) {
        uint64_t code;
        uint8_t bits;
        uint16_t T;
        if (try_decode_protocol(&rc_protos[p], rx_timings, rx_count,
                                &code, &bits, &T)) {
            if (best_proto_idx < 0 || bits > best_bits) {
                best_proto_idx = p;
                best_code = code;
                best_bits = bits;
                best_T = T;
            }
        }
    }

    /* Dedup: suppress same bit-length within window */
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (best_proto_idx >= 0 && best_bits == last_rx_bits &&
        (now - last_rx_time) < DEDUP_MS) {
        return;
    }

    if (best_proto_idx >= 0) {
        last_rx_bits = best_bits;
        last_rx_time = now;

        ble_rf_mini_notify_signal(rc_protos[best_proto_idx].id,
                                  best_code, best_bits, best_T, current_freq);
        ESP_LOGI(TAG, "RX decoded: P%d key=0x%llX %dbit T=%d freq=%.2f",
                 rc_protos[best_proto_idx].id,
                 (unsigned long long)best_code, best_bits, best_T, current_freq);
    } else {
        /* Send raw timings (0x81) */
        uint16_t durations[256];
        uint8_t n = 0;
        for (int i = 0; i < rx_count && n < 255; i++) {
            uint16_t d = (rx_timings[i] > 65535) ? 65535 : (uint16_t)rx_timings[i];
            durations[n++] = d;
        }
        if (n >= 6) {
            ble_rf_mini_notify_raw(current_freq, durations, n, 1);
            ESP_LOGI(TAG, "RX raw: %d pulses freq=%.2f", n, current_freq);
        }
    }
}

static void process_rx_pulse(uint32_t dur)
{
    if (dur > RX_SEPARATION) {
        if (rx_got_sync && rx_count > 6) {
            handle_frame();
        }
        rx_count = 0;
        rx_timings[rx_count++] = (unsigned int)dur;
        rx_got_sync = true;
    } else if (rx_got_sync && rx_count < RX_MAX_TIM) {
        rx_timings[rx_count++] = (unsigned int)dur;
    }
}

/* ===== RF task ===== */

static void rf_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "RF task started");

    cc1101_pulse_t pulses[64];
    while (1) {
        if (current_state == STATE_LISTENING || current_state == STATE_SCANNING) {
            int n = cc1101_get_pulses(pulses, 64);
            for (int i = 0; i < n; i++) {
                process_rx_pulse(pulses[i].duration_us);
            }
        }
        if (current_state == STATE_SCANNING) {
            poll_scan();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/* ===== LED status task ===== */

static void led_task(void *param)
{
    (void)param;
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    LED_OFF();

    while (1) {
        if (led_suppress) {
            LED_OFF();
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        bool connected = ble_rf_mini_connected();
        if (connected) {
            switch (current_state) {
            case STATE_LISTENING:
            case STATE_SCANNING:
                /* slow blink: on 100ms, off 900ms */
                LED_ON(); vTaskDelay(pdMS_TO_TICKS(100));
                LED_OFF(); vTaskDelay(pdMS_TO_TICKS(900));
                break;
            case STATE_TRANSMITTING:
                /* fast blink */
                LED_ON(); vTaskDelay(pdMS_TO_TICKS(50));
                LED_OFF(); vTaskDelay(pdMS_TO_TICKS(50));
                break;
            default:
                /* connected idle: solid on */
                LED_ON(); vTaskDelay(pdMS_TO_TICKS(200));
                break;
            }
        } else {
            /* not connected: double blink every 2s */
            LED_ON(); vTaskDelay(pdMS_TO_TICKS(80));
            LED_OFF(); vTaskDelay(pdMS_TO_TICKS(80));
            LED_ON(); vTaskDelay(pdMS_TO_TICKS(80));
            LED_OFF(); vTaskDelay(pdMS_TO_TICKS(1760));
        }
    }
}

/* ===== Sleep: long press BOOT or 60s no connection ===== */

static void enter_sleep(void)
{
    ESP_LOGW(TAG, "Deep sleep. Tap BOOT to wake (quick release!)");
    LED_OFF();
    cc1101_set_idle_state();

    esp_deep_sleep_enable_gpio_wakeup(1ULL << WAKE_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
}

static void sleep_monitor_task(void *param)
{
    (void)param;
    gpio_reset_pin(BOOT_PIN);
    gpio_set_direction(BOOT_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOOT_PIN, GPIO_PULLUP_ONLY);

    gpio_reset_pin(WAKE_PIN);
    gpio_set_direction(WAKE_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(WAKE_PIN, GPIO_PULLUP_ONLY);

    last_connect_activity = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t press_start = 0;
    uint32_t last_dbg = 0;

    while (1) {
        int boot_level = gpio_get_level(BOOT_PIN);
        int wake_level = gpio_get_level(WAKE_PIN);
        bool connected = ble_rf_mini_connected();
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        /* debug every 2s */
        if ((now - last_dbg) >= 2000) {
            last_dbg = now;
            uint32_t idle_sec = (now - last_connect_activity) / 1000;
            ESP_LOGI(TAG, "SLEEP_MON: BOOT=%d GPIO0=%d conn=%d idle=%lus press=%lums",
                     boot_level, wake_level, connected, (unsigned long)idle_sec,
                     press_start ? (unsigned long)(now - press_start) : 0UL);
        }

        if (connected) {
            last_connect_activity = now;
        }

        /* long press via GPIO0 (mirrored from BOOT) -> sleep */
        bool pressed = (wake_level == 0);
        if (pressed) {
            if (press_start == 0) {
                press_start = now;
                ESP_LOGI(TAG, "BOOT pressed (GPIO0=LOW)");
            }
            if ((now - press_start) >= LONG_PRESS_MS) {
                ESP_LOGW(TAG, "Long press -> waiting for release...");
                led_suppress = true;
                while (gpio_get_level(WAKE_PIN) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                ESP_LOGW(TAG, "Released -> SLEEP");
                enter_sleep();
            }
        } else {
            press_start = 0;
        }

        /* no connection for 60s -> sleep */
        if (!connected && (now - last_connect_activity) >= SLEEP_TIMEOUT_MS) {
            ESP_LOGW(TAG, "No BLE for %ds -> SLEEP", SLEEP_TIMEOUT_MS / 1000);
            while (gpio_get_level(WAKE_PIN) == 0) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            enter_sleep();
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ===== Main ===== */

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Mini-Bruce BLE+RF (ESP32-C3+CC1101)");
    ESP_LOGI(TAG, "========================================");

    board_init();

    cc1101_config_t cc_cfg = {
        .spi_host = CC1101_SPI_HOST,
        .pin_cs   = CC1101_PIN_CS,
        .pin_gdo0 = CC1101_PIN_GDO0,
        .pin_gdo2 = CC1101_PIN_GDO2,
        .bus_already_initialized = SPI_BUS_SHARED,
        .pin_sck  = CC1101_PIN_SCK,
        .pin_mosi = CC1101_PIN_MOSI,
        .pin_miso = CC1101_PIN_MISO,
    };

    esp_err_t ret = cc1101_init(&cc_cfg, CFREQ_433, CSPEED_4800);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CC1101 init failed!");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    uint8_t partnum, version;
    cc1101_check_chip(&partnum, &version);
    cc1101_set_tx_power(POWER_MAX);

    ble_rf_mini_init("Bruce-RF", on_ble_cmd);

    xTaskCreate(rf_task, "RF", 1024 * 8, NULL, 5, NULL);
    xTaskCreate(led_task, "LED", 1024 * 2, NULL, 1, NULL);
    xTaskCreate(sleep_monitor_task, "SLEEP", 1024 * 2, NULL, 1, NULL);

    ESP_LOGI(TAG, "Ready. Connect via BLE to 'Bruce-RF'");
    ESP_LOGI(TAG, "LED: double-blink=waiting, solid=connected, slow-blink=RX, fast=TX");
    ESP_LOGI(TAG, "Long-press BOOT or 60s no BLE -> deep sleep");
}
