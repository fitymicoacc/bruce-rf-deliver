#ifndef LIL_H
#define LIL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Status codes
 * ========================================================================= */
typedef enum {
    LIL_OK                = 0,
    LIL_ERR_BUF_TOO_SMALL = -1,
    LIL_ERR_BAD_FRAME     = -2,
    LIL_ERR_BAD_ARG       = -3,
    LIL_ERR_NO_MATCH      = -4
} lil_status_t;

/* =========================================================================
 * Wire protocol constants — must match bruce-rf/src/modules/ble_rf/
 *   ble_rf_service.h and desktop/src/renderer/src/lib/constants.ts
 * ========================================================================= */
#define LIL_SERVICE_UUID     "12345678-1234-5678-1234-56789abcdef0"
#define LIL_CMD_CHAR_UUID    "12345678-1234-5678-1234-56789abcdef1"
#define LIL_SIGNAL_CHAR_UUID "12345678-1234-5678-1234-56789abcdef2"
#define LIL_STATUS_CHAR_UUID "12345678-1234-5678-1234-56789abcdef3"

#define LIL_SIGNAL_HEADER      0x80u
#define LIL_RAW_TIMINGS_HEADER 0x81u

#define LIL_SIGNAL_PACKET_SIZE  17u
#define LIL_STATUS_PACKET_SIZE   2u
#define LIL_RAW_TIMINGS_HDR_SIZE 7u  /* header(1)+freq(4)+count(1)+level(1) */

#define LIL_MAX_SCAN_FREQS   32u
#define LIL_MAX_SCAN_RANGES   8u
#define LIL_MAX_RAW_TIMINGS 256u  /* decoder input cap */

/* =========================================================================
 * Commands (host → device)
 * ========================================================================= */
typedef enum {
    LIL_CMD_START_LISTEN = 0x01,
    LIL_CMD_STOP         = 0x02,
    LIL_CMD_PLAY         = 0x03,
    LIL_CMD_PING         = 0x04,
    LIL_CMD_SCAN_RANGES  = 0x05,
    LIL_CMD_SCAN_LIST    = 0x06
} lil_cmd_id_t;

/* =========================================================================
 * Device state + error enums
 * ========================================================================= */
typedef enum {
    LIL_STATE_IDLE         = 0x00,
    LIL_STATE_LISTENING    = 0x01,
    LIL_STATE_TRANSMITTING = 0x02,
    LIL_STATE_SCANNING     = 0x03,
    LIL_STATE_ERROR        = 0xFF
} lil_device_state_t;

typedef enum {
    LIL_DEV_ERR_NONE        = 0x00,
    LIL_DEV_ERR_CC1101_FAIL = 0x01,
    LIL_DEV_ERR_INVALID_CMD = 0x02,
    LIL_DEV_ERR_TX_FAIL     = 0x03
} lil_device_error_t;

/* =========================================================================
 * Data structures
 * ========================================================================= */

/* A decoded RF signal. Matches 0x80 notification payload and CMD_PLAY body. */
typedef struct {
    uint8_t  protocol;     /* RCSwitch protocol id (1..24), 0 = unknown/RAW */
    uint64_t key;          /* signal code, LSB is last bit */
    uint8_t  bits;         /* data bits in key */
    uint16_t pulse_length; /* base T in microseconds */
    float    freq;         /* MHz */
} lil_signal_t;

typedef struct {
    float start;
    float end;
    float step;
} lil_freq_range_t;

/* Raw timings frame (header 0x81). Decoder input. */
typedef struct {
    float    freq;
    uint8_t  start_level;               /* 0 = LOW, 1 = HIGH */
    uint8_t  count;
    uint16_t durations[LIL_MAX_RAW_TIMINGS]; /* microseconds, LOW/HIGH alternating */
} lil_raw_timings_t;

/* =========================================================================
 * RCSwitch protocol table access
 * ========================================================================= */
typedef struct {
    uint8_t     id;            /* 1..24 */
    const char* name;
    uint16_t    pulse_length;  /* base T in microseconds */
    uint8_t     sync_h;
    uint8_t     sync_l;
    uint8_t     zero_h;
    uint8_t     zero_l;
    uint8_t     one_h;
    uint8_t     one_l;
    uint8_t     inverted;      /* 0 or 1 */
} lil_rc_protocol_t;

/* Count of protocols in the built-in table. */
size_t lil_protocol_count(void);

/* Get protocol by index (0..count-1). Returns NULL if idx out of range. */
const lil_rc_protocol_t* lil_protocol_at(size_t idx);

/* Get protocol by id (1..24). Returns NULL if id not in table. */
const lil_rc_protocol_t* lil_protocol_by_id(uint8_t id);

/* =========================================================================
 * Pack: host → device
 *   All functions write to `buf` of `buf_len` bytes; `*out_len` receives
 *   the number of bytes actually written. Return LIL_ERR_BUF_TOO_SMALL
 *   if insufficient space (and `*out_len` holds the required size).
 * ========================================================================= */

/* 5 bytes: cmd(1) + freq(float32 LE). */
lil_status_t lil_pack_start_listen(float freq,
                                   uint8_t* buf, size_t buf_len, size_t* out_len);

/* 1 byte: cmd. */
lil_status_t lil_pack_stop(uint8_t* buf, size_t buf_len, size_t* out_len);

/* 1 byte: cmd. */
lil_status_t lil_pack_ping(uint8_t* buf, size_t buf_len, size_t* out_len);

/* 17 bytes: cmd(1) + proto(1) + key(u64 LE) + bits(1) + pulse(u16 LE) + freq(f32 LE). */
lil_status_t lil_pack_play(const lil_signal_t* sig,
                           uint8_t* buf, size_t buf_len, size_t* out_len);

/* Variable: cmd(1) + dwell_ms(u16 LE) + count(1) + [start(f32) end(f32) step(f32)]*count. */
lil_status_t lil_pack_scan_ranges(const lil_freq_range_t* ranges, uint8_t count,
                                  uint16_t dwell_ms,
                                  uint8_t* buf, size_t buf_len, size_t* out_len);

/* Variable: cmd(1) + dwell_ms(u16 LE) + count(1) + freq(f32 LE)*count. */
lil_status_t lil_pack_scan_list(const float* freqs, uint8_t count,
                                uint16_t dwell_ms,
                                uint8_t* buf, size_t buf_len, size_t* out_len);

/* =========================================================================
 * Unpack: device → host
 * ========================================================================= */

/* 17 bytes starting with 0x80: decoded signal from device. */
lil_status_t lil_unpack_signal(const uint8_t* data, size_t len,
                               lil_signal_t* out);

/* >=7 bytes starting with 0x81: raw RMT timings. `count` can be fewer than
   LIL_MAX_RAW_TIMINGS; any overflow is silently truncated. */
lil_status_t lil_unpack_raw_timings(const uint8_t* data, size_t len,
                                    lil_raw_timings_t* out);

/* 2 bytes: state + error. */
lil_status_t lil_unpack_status(const uint8_t* data, size_t len,
                               lil_device_state_t* state,
                               lil_device_error_t* error);

/* =========================================================================
 * Decoder: brute-force RCSwitch protocol matching on raw timings.
 *   Ports desktop/src/renderer/src/lib/decoder.ts semantics. Pre-glitches
 *   the input (pulses < 30us are merged with neighbours), finds sync
 *   candidates, tries every protocol × both H/L orientations, returns the
 *   match with the highest score (prefers standard bit counts).
 * ========================================================================= */
lil_status_t lil_decode_raw_timings(const uint16_t* durations, size_t count,
                                    uint8_t start_level, float freq,
                                    lil_signal_t* out);

#ifdef __cplusplus
}
#endif

#endif /* LIL_H */
