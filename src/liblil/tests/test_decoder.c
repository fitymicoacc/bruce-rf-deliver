#include "lil.h"
#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * Notes on brute-force ambiguity
 *
 * The decoder (like its TypeScript reference) scores matches on bit count
 * alone — there is no total_deviation tiebreak. For protocols with similar
 * sync signatures (PT2262 ↔ FAAC ↔ NICE ↔ Blyss all match 12/24-bit frames
 * with moderately long sync) brute force picks the first in the protocol
 * table, i.e. PT2262. The `key` and `bits` come out correct; the
 * `protocol` field identifies whichever definition matched first, not
 * necessarily the emitting one.
 *
 * Separately, protocols with very short sync (CAME 12, Protocol 23) have
 * data pulses longer than half the sync pulse — those get classified as
 * "next sync" and the decoder stops. This is baseline behaviour from the
 * reference and is out of scope for the port.
 *
 * Regression cases here therefore focus on:
 *   - protocols whose sync is long enough and distinct enough to match
 *     uniquely (PT2262, EV1527, HX2262);
 *   - behaviour cases that don't depend on protocol identity (key/bits
 *     extraction, off-by-one padding, reject paths).
 * --------------------------------------------------------------------------- */

static size_t synth_raw(const lil_rc_protocol_t* p,
                        uint64_t code, int bits,
                        uint16_t* out)
{
    size_t n = 0;
    int T = p->pulse_length;
    out[n++] = (uint16_t)T;              /* prepended sync HIGH (bruce-rf pollRx invariant) */
    out[n++] = (uint16_t)(T * p->sync_l);/* sync LOW */
    for (int b = bits - 1; b >= 0; b--) {
        int bit = (int)((code >> b) & 1ull);
        if (bit) {
            out[n++] = (uint16_t)(T * p->one_h);
            out[n++] = (uint16_t)(T * p->one_l);
        } else {
            out[n++] = (uint16_t)(T * p->zero_h);
            out[n++] = (uint16_t)(T * p->zero_l);
        }
    }
    return n;
}

static void assert_full_match(uint8_t proto_id, uint64_t code, int bits) {
    const lil_rc_protocol_t* p = lil_protocol_by_id(proto_id);
    TEST_ASSERT_NOT_NULL(p);
    uint16_t raw[LIL_MAX_RAW_TIMINGS];
    size_t n = synth_raw(p, code, bits, raw);

    lil_signal_t out = {0};
    TEST_ASSERT_EQUAL_INT_MESSAGE(LIL_OK,
        lil_decode_raw_timings(raw, n, 1, 433.92f, &out), p->name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(proto_id, out.protocol, p->name);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(code, out.key, p->name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(bits, out.bits, p->name);
}

/* Matches protocol id, key, and bits exactly. */
static void test_decode_pt2262_24bit(void) {
    assert_full_match(1, 0xA1B2C5ull, 24);
}

static void test_decode_ev1527_24bit(void) {
    assert_full_match(4, 0x55AA33ull, 24);
}

static void test_decode_hx2262_key_and_bits(void) {
    /* HX2262 (sync=[30,71], T=100) overlaps PT2262 by score. The best
     * brute-force candidate will fix key + bits but may report PT2262. */
    const lil_rc_protocol_t* p = lil_protocol_by_id(3);
    TEST_ASSERT_NOT_NULL(p);
    uint16_t raw[LIL_MAX_RAW_TIMINGS];
    size_t n = synth_raw(p, 0x010203ull, 24, raw);

    lil_signal_t out = {0};
    TEST_ASSERT_EQUAL_INT(LIL_OK,
        lil_decode_raw_timings(raw, n, 1, 433.92f, &out));
    TEST_ASSERT_EQUAL_UINT64(0x010203ull, out.key);
    TEST_ASSERT_EQUAL_UINT8(24, out.bits);
}

static void test_decode_rejects_too_short_buffer(void) {
    uint16_t raw[4] = { 100, 100, 100, 100 };
    lil_signal_t out = {0};
    TEST_ASSERT_EQUAL_INT(LIL_ERR_NO_MATCH,
        lil_decode_raw_timings(raw, 4, 1, 433.92f, &out));
}

static void test_decode_rejects_null_outputs(void) {
    uint16_t raw[16] = {0};
    TEST_ASSERT_EQUAL_INT(LIL_ERR_BAD_ARG,
        lil_decode_raw_timings(raw, 16, 1, 433.92f, NULL));
    lil_signal_t out = {0};
    TEST_ASSERT_EQUAL_INT(LIL_ERR_BAD_ARG,
        lil_decode_raw_timings(NULL, 16, 1, 433.92f, &out));
}

static void test_decode_pads_off_by_one(void) {
    /* Lose the first data pair. Decoder finds 23 bits and pads the count
     * to the next standard value (24). Key value shifts — we only assert
     * the bit count snap. */
    const lil_rc_protocol_t* p = lil_protocol_by_id(1);
    TEST_ASSERT_NOT_NULL(p);
    uint16_t raw[LIL_MAX_RAW_TIMINGS];
    size_t n = synth_raw(p, 0xA1B2C5ull, 24, raw);
    memmove(&raw[2], &raw[4], (n - 4) * sizeof(raw[0]));
    n -= 2;
    lil_signal_t out = {0};
    TEST_ASSERT_EQUAL_INT(LIL_OK,
        lil_decode_raw_timings(raw, n, 1, 433.92f, &out));
    TEST_ASSERT_EQUAL_UINT8(24, out.bits);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_decode_pt2262_24bit);
    RUN_TEST(test_decode_ev1527_24bit);
    RUN_TEST(test_decode_hx2262_key_and_bits);
    RUN_TEST(test_decode_rejects_too_short_buffer);
    RUN_TEST(test_decode_rejects_null_outputs);
    RUN_TEST(test_decode_pads_off_by_one);
    return UNITY_END();
}
