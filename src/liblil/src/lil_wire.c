#include "lil.h"

#include <string.h>

/* All multi-byte fields on the wire are little-endian. Every host we target
 * is LE (x86_64, arm, aarch64) so `memcpy` + the LE memory layout matches
 * the wire. When we port to a BE host the helpers below become byte-swappy. */

static void put_u16(uint8_t* dst, uint16_t v) {
    dst[0] = (uint8_t)(v & 0xFF);
    dst[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void put_u64(uint8_t* dst, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        dst[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
    }
}

static void put_f32(uint8_t* dst, float v) {
    uint32_t raw;
    memcpy(&raw, &v, sizeof(raw));
    for (int i = 0; i < 4; i++) {
        dst[i] = (uint8_t)((raw >> (i * 8)) & 0xFF);
    }
}

static uint16_t get_u16(const uint8_t* src) {
    return (uint16_t)(src[0] | ((uint16_t)src[1] << 8));
}

static uint64_t get_u64(const uint8_t* src) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v |= ((uint64_t)src[i]) << (i * 8);
    }
    return v;
}

static float get_f32(const uint8_t* src) {
    uint32_t raw = 0;
    for (int i = 0; i < 4; i++) {
        raw |= ((uint32_t)src[i]) << (i * 8);
    }
    float v;
    memcpy(&v, &raw, sizeof(v));
    return v;
}

/* =========================================================================
 * Pack
 * ========================================================================= */

static lil_status_t pack_fit(size_t need, size_t have, size_t* out_len) {
    if (out_len) *out_len = need;
    return (have < need) ? LIL_ERR_BUF_TOO_SMALL : LIL_OK;
}

lil_status_t lil_pack_start_listen(float freq,
                                   uint8_t* buf, size_t buf_len, size_t* out_len) {
    if (!buf) return LIL_ERR_BAD_ARG;
    const size_t need = 5;
    lil_status_t st = pack_fit(need, buf_len, out_len);
    if (st != LIL_OK) return st;
    buf[0] = (uint8_t)LIL_CMD_START_LISTEN;
    put_f32(&buf[1], freq);
    return LIL_OK;
}

lil_status_t lil_pack_stop(uint8_t* buf, size_t buf_len, size_t* out_len) {
    if (!buf) return LIL_ERR_BAD_ARG;
    const size_t need = 1;
    lil_status_t st = pack_fit(need, buf_len, out_len);
    if (st != LIL_OK) return st;
    buf[0] = (uint8_t)LIL_CMD_STOP;
    return LIL_OK;
}

lil_status_t lil_pack_ping(uint8_t* buf, size_t buf_len, size_t* out_len) {
    if (!buf) return LIL_ERR_BAD_ARG;
    const size_t need = 1;
    lil_status_t st = pack_fit(need, buf_len, out_len);
    if (st != LIL_OK) return st;
    buf[0] = (uint8_t)LIL_CMD_PING;
    return LIL_OK;
}

lil_status_t lil_pack_play(const lil_signal_t* sig,
                           uint8_t* buf, size_t buf_len, size_t* out_len) {
    if (!sig || !buf) return LIL_ERR_BAD_ARG;
    const size_t need = 17;
    lil_status_t st = pack_fit(need, buf_len, out_len);
    if (st != LIL_OK) return st;
    size_t off = 0;
    buf[off++] = (uint8_t)LIL_CMD_PLAY;
    buf[off++] = sig->protocol;
    put_u64(&buf[off], sig->key);   off += 8;
    buf[off++] = sig->bits;
    put_u16(&buf[off], sig->pulse_length); off += 2;
    put_f32(&buf[off], sig->freq);  off += 4;
    (void)off;
    return LIL_OK;
}

lil_status_t lil_pack_scan_ranges(const lil_freq_range_t* ranges, uint8_t count,
                                  uint16_t dwell_ms,
                                  uint8_t* buf, size_t buf_len, size_t* out_len) {
    if (!buf || (count > 0 && !ranges)) return LIL_ERR_BAD_ARG;
    if (count > LIL_MAX_SCAN_RANGES) return LIL_ERR_BAD_ARG;
    const size_t need = (size_t)4 + (size_t)count * 12u;
    lil_status_t st = pack_fit(need, buf_len, out_len);
    if (st != LIL_OK) return st;
    size_t off = 0;
    buf[off++] = (uint8_t)LIL_CMD_SCAN_RANGES;
    put_u16(&buf[off], dwell_ms); off += 2;
    buf[off++] = count;
    for (uint8_t i = 0; i < count; i++) {
        put_f32(&buf[off], ranges[i].start); off += 4;
        put_f32(&buf[off], ranges[i].end);   off += 4;
        put_f32(&buf[off], ranges[i].step);  off += 4;
    }
    (void)off;
    return LIL_OK;
}

lil_status_t lil_pack_scan_list(const float* freqs, uint8_t count,
                                uint16_t dwell_ms,
                                uint8_t* buf, size_t buf_len, size_t* out_len) {
    if (!buf || (count > 0 && !freqs)) return LIL_ERR_BAD_ARG;
    if (count > LIL_MAX_SCAN_FREQS) return LIL_ERR_BAD_ARG;
    const size_t need = (size_t)4 + (size_t)count * 4u;
    lil_status_t st = pack_fit(need, buf_len, out_len);
    if (st != LIL_OK) return st;
    size_t off = 0;
    buf[off++] = (uint8_t)LIL_CMD_SCAN_LIST;
    put_u16(&buf[off], dwell_ms); off += 2;
    buf[off++] = count;
    for (uint8_t i = 0; i < count; i++) {
        put_f32(&buf[off], freqs[i]); off += 4;
    }
    (void)off;
    return LIL_OK;
}

/* =========================================================================
 * Unpack
 * ========================================================================= */

lil_status_t lil_unpack_signal(const uint8_t* data, size_t len,
                               lil_signal_t* out) {
    if (!data || !out) return LIL_ERR_BAD_ARG;
    if (len < LIL_SIGNAL_PACKET_SIZE) return LIL_ERR_BAD_FRAME;
    if (data[0] != LIL_SIGNAL_HEADER) return LIL_ERR_BAD_FRAME;
    size_t off = 1;
    out->protocol     = data[off++];
    out->key          = get_u64(&data[off]); off += 8;
    out->bits         = data[off++];
    out->pulse_length = get_u16(&data[off]); off += 2;
    out->freq         = get_f32(&data[off]); /* off += 4; */
    return LIL_OK;
}

lil_status_t lil_unpack_raw_timings(const uint8_t* data, size_t len,
                                    lil_raw_timings_t* out) {
    if (!data || !out) return LIL_ERR_BAD_ARG;
    if (len < LIL_RAW_TIMINGS_HDR_SIZE) return LIL_ERR_BAD_FRAME;
    if (data[0] != LIL_RAW_TIMINGS_HEADER) return LIL_ERR_BAD_FRAME;

    out->freq        = get_f32(&data[1]);
    uint8_t declared = data[5];
    out->start_level = data[6];

    size_t payload_bytes = len - LIL_RAW_TIMINGS_HDR_SIZE;
    size_t available = payload_bytes / 2;
    size_t count = declared;
    if (count > available) count = available;
    if (count > LIL_MAX_RAW_TIMINGS) count = LIL_MAX_RAW_TIMINGS;

    out->count = (uint8_t)count;
    for (size_t i = 0; i < count; i++) {
        out->durations[i] = get_u16(&data[LIL_RAW_TIMINGS_HDR_SIZE + i * 2]);
    }
    return LIL_OK;
}

lil_status_t lil_unpack_status(const uint8_t* data, size_t len,
                               lil_device_state_t* state,
                               lil_device_error_t* error) {
    if (!data || !state || !error) return LIL_ERR_BAD_ARG;
    if (len < LIL_STATUS_PACKET_SIZE) return LIL_ERR_BAD_FRAME;
    *state = (lil_device_state_t)data[0];
    *error = (lil_device_error_t)data[1];
    return LIL_OK;
}
