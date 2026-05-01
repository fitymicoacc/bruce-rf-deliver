#include "lil.h"
#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_pack_stop_one_byte(void) {
    uint8_t buf[4] = {0};
    size_t written = 0;
    TEST_ASSERT_EQUAL_INT(LIL_OK, lil_pack_stop(buf, sizeof(buf), &written));
    TEST_ASSERT_EQUAL_UINT(1u, (unsigned)written);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[0]);
}

static void test_pack_start_listen_layout(void) {
    uint8_t buf[5] = {0};
    size_t written = 0;
    TEST_ASSERT_EQUAL_INT(LIL_OK, lil_pack_start_listen(433.92f, buf, sizeof(buf), &written));
    TEST_ASSERT_EQUAL_UINT(5u, (unsigned)written);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);

    float freq;
    memcpy(&freq, &buf[1], sizeof(freq));
    TEST_ASSERT_EQUAL_FLOAT(433.92f, freq);
}

static void test_pack_play_roundtrip(void) {
    lil_signal_t sig = {
        .protocol = 1, .key = 0xA1B2C5ull, .bits = 24,
        .pulse_length = 349, .freq = 433.92f
    };
    uint8_t buf[17] = {0};
    size_t written = 0;
    TEST_ASSERT_EQUAL_INT(LIL_OK, lil_pack_play(&sig, buf, sizeof(buf), &written));
    TEST_ASSERT_EQUAL_UINT(17u, (unsigned)written);
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(1,    buf[1]);

    /* Signal notification header is different (0x80), but the body layout
       is identical — reuse lil_unpack_signal by building a fake notify. */
    uint8_t notify[LIL_SIGNAL_PACKET_SIZE];
    notify[0] = LIL_SIGNAL_HEADER;
    memcpy(&notify[1], &buf[1], LIL_SIGNAL_PACKET_SIZE - 1);

    lil_signal_t decoded = {0};
    TEST_ASSERT_EQUAL_INT(LIL_OK, lil_unpack_signal(notify, sizeof(notify), &decoded));
    TEST_ASSERT_EQUAL_UINT8(sig.protocol, decoded.protocol);
    TEST_ASSERT_EQUAL_UINT64(sig.key,     decoded.key);
    TEST_ASSERT_EQUAL_UINT8(sig.bits,     decoded.bits);
    TEST_ASSERT_EQUAL_UINT16(sig.pulse_length, decoded.pulse_length);
    TEST_ASSERT_EQUAL_FLOAT(sig.freq, decoded.freq);
}

static void test_pack_scan_list_layout(void) {
    float freqs[3] = { 315.0f, 433.92f, 868.0f };
    uint8_t buf[64] = {0};
    size_t written = 0;
    TEST_ASSERT_EQUAL_INT(LIL_OK, lil_pack_scan_list(freqs, 3, 200, buf, sizeof(buf), &written));
    TEST_ASSERT_EQUAL_UINT((unsigned)(4 + 3 * 4), (unsigned)written);
    TEST_ASSERT_EQUAL_HEX8(0x06, buf[0]);
    uint16_t dwell = (uint16_t)(buf[1] | (buf[2] << 8));
    TEST_ASSERT_EQUAL_UINT16(200, dwell);
    TEST_ASSERT_EQUAL_UINT8(3,     buf[3]);
}

static void test_pack_buffer_too_small_reports_required(void) {
    uint8_t tiny[2] = {0};
    size_t written = 0;
    TEST_ASSERT_EQUAL_INT(LIL_ERR_BUF_TOO_SMALL,
                          lil_pack_start_listen(433.92f, tiny, sizeof(tiny), &written));
    TEST_ASSERT_EQUAL_UINT(5u, (unsigned)written);
}

static void test_unpack_status(void) {
    uint8_t data[2] = { 0x01, 0x00 };
    lil_device_state_t state;
    lil_device_error_t error;
    TEST_ASSERT_EQUAL_INT(LIL_OK, lil_unpack_status(data, sizeof(data), &state, &error));
    TEST_ASSERT_EQUAL_INT(LIL_STATE_LISTENING, state);
    TEST_ASSERT_EQUAL_INT(LIL_DEV_ERR_NONE,    error);
}

static void test_unpack_raw_timings(void) {
    uint8_t buf[LIL_RAW_TIMINGS_HDR_SIZE + 4 * 2];
    buf[0] = LIL_RAW_TIMINGS_HEADER;
    float freq = 433.92f;
    memcpy(&buf[1], &freq, sizeof(freq));
    buf[5] = 4;    /* count */
    buf[6] = 1;    /* start_level */
    /* 4 LE uint16 timings */
    const uint16_t expected[4] = { 350, 1050, 1050, 350 };
    for (size_t i = 0; i < 4; i++) {
        buf[LIL_RAW_TIMINGS_HDR_SIZE + i * 2]     = (uint8_t)(expected[i] & 0xFF);
        buf[LIL_RAW_TIMINGS_HDR_SIZE + i * 2 + 1] = (uint8_t)(expected[i] >> 8);
    }

    lil_raw_timings_t rt = {0};
    TEST_ASSERT_EQUAL_INT(LIL_OK, lil_unpack_raw_timings(buf, sizeof(buf), &rt));
    TEST_ASSERT_EQUAL_FLOAT(433.92f, rt.freq);
    TEST_ASSERT_EQUAL_UINT8(1, rt.start_level);
    TEST_ASSERT_EQUAL_UINT8(4, rt.count);
    for (size_t i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_UINT16(expected[i], rt.durations[i]);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pack_stop_one_byte);
    RUN_TEST(test_pack_start_listen_layout);
    RUN_TEST(test_pack_play_roundtrip);
    RUN_TEST(test_pack_scan_list_layout);
    RUN_TEST(test_pack_buffer_too_small_reports_required);
    RUN_TEST(test_unpack_status);
    RUN_TEST(test_unpack_raw_timings);
    return UNITY_END();
}
