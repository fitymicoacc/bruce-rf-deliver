#pragma once

#include <stdint.h>
#include <stdbool.h>

#define BLE_RF_SERVICE_UUID     0xDEF0
#define BLE_RF_CMD_CHAR_UUID    0xDEF1
#define BLE_RF_SIGNAL_CHAR_UUID 0xDEF2
#define BLE_RF_STATUS_CHAR_UUID 0xDEF3

#define CMD_START_LISTEN  0x01
#define CMD_STOP          0x02
#define CMD_PLAY          0x03
#define CMD_PING          0x04
#define CMD_SCAN_RANGES   0x05
#define CMD_SCAN_LIST     0x06

#define STATE_IDLE         0x00
#define STATE_LISTENING    0x01
#define STATE_TRANSMITTING 0x02
#define STATE_SCANNING     0x03
#define STATE_ERROR        0xFF

#define ERR_NONE           0x00
#define ERR_CC1101_FAIL    0x01
#define ERR_INVALID_CMD    0x02
#define ERR_TX_FAIL        0x03

#define SIGNAL_HDR         0x80
#define RAW_TIMINGS_HDR    0x81

#define MAX_SCAN_FREQS  32
#define MAX_SCAN_RANGES  8

typedef struct {
    float start, end, step;
} ble_rf_freq_range_t;

typedef struct {
    uint8_t  cmd;
    float    freq;
    uint8_t  protocol;
    uint64_t key;
    uint8_t  bits;
    uint16_t pulse_length;
    /* SCAN_LIST / SCAN_RANGES */
    uint16_t dwell_ms;
    uint8_t  scan_count;
    float    scan_freqs[MAX_SCAN_FREQS];
    ble_rf_freq_range_t scan_ranges[MAX_SCAN_RANGES];
} ble_rf_cmd_t;

typedef void (*ble_rf_cmd_cb_t)(const ble_rf_cmd_t *cmd);

void ble_rf_mini_init(const char *device_name, ble_rf_cmd_cb_t on_cmd);
bool ble_rf_mini_connected(void);
void ble_rf_mini_notify_signal(uint8_t proto, uint64_t key, uint8_t bits,
                               uint16_t pulse, float freq);
void ble_rf_mini_notify_raw(float freq, const uint16_t *durations,
                            uint8_t count, uint8_t start_level);
void ble_rf_mini_notify_status(uint8_t state, uint8_t error);
