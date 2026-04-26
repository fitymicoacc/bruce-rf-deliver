#include "lil.h"

#include <stdlib.h>
#include <string.h>

/* Port of desktop/src/renderer/src/lib/decoder.ts.
 *
 * Input is a raw CC1101/RMT pulse train: HIGH/LOW durations in microseconds
 * alternating. The encoder is OOK (on-off keyed). Each protocol from the
 * RCSwitch table defines sync/zero/one as (H,L) multipliers of a base T.
 *
 * Strategy (brute-force, matches TS reference):
 *   1. Pre-process: merge pulses shorter than 30us with neighbours — these
 *      are SPI/BLE ISR glitches.
 *   2. Group durations into (H,L) pairs.
 *   3. Find sync candidates: pairs whose max component is > median × 4.
 *      If none — fall back to the pair with the single largest component.
 *   4. For every sync candidate × every protocol × both orientations
 *      (dur0 is HIGH or LOW), compute T from sync, verify sync duration
 *      tolerance, then readBits until a pulse fails the threshold.
 *   5. Score each successful match: standard bit counts (8/12/16/20/24/28/
 *      32/40/48/64) get 150 points per bit, non-standard 50. After-sync
 *      reads get +50. Return the highest-scoring result.
 *
 * The TS reference uses floats throughout; we use ints everywhere the math
 * allows. Tolerance scaling is kept in permille to avoid floating-point on
 * every pulse.
 */

#define GLITCH_THRESHOLD 30u
#define SYNC_TOL_PCT     40     /* %  — sync duration tolerance */
#define BIT_THRESH_PCT   60     /* %  — pulse deviation threshold */
#define HIGH_W           3      /* AGC weight for HIGH half */
#define MIN_T            50
#define MAX_T            2000
#define MAX_PAIRS        (LIL_MAX_RAW_TIMINGS / 2)

/* Standard bit counts we score higher — filters out 25/30-bit matches that
 * come from reading past the real data boundary. */
static const int kStdBits[] = {8, 12, 16, 20, 24, 28, 32, 40, 48, 64};
#define kStdBitsCount (sizeof(kStdBits) / sizeof(kStdBits[0]))

static int is_std_bits(int bits) {
    for (size_t i = 0; i < kStdBitsCount; i++) {
        if (kStdBits[i] == bits) return 1;
    }
    return 0;
}

/* If bits is exactly one below a standard count (RMT capture missed the
 * first pulse), pad to that count. */
static int pad_off_by_one(int bits) {
    for (size_t i = 0; i < kStdBitsCount; i++) {
        if (bits == kStdBits[i] - 1) return kStdBits[i];
    }
    return bits;
}

static int cmp_u16(const void* a, const void* b) {
    uint16_t aa = *(const uint16_t*)a;
    uint16_t bb = *(const uint16_t*)b;
    if (aa < bb) return -1;
    if (aa > bb) return 1;
    return 0;
}

static uint16_t u16_max(uint16_t a, uint16_t b) { return (a > b) ? a : b; }

/* Merge pulses < GLITCH_THRESHOLD with their neighbours. Mirrors the TS
 * deglitch() helper. Returns the count of cleaned durations in `out`. */
static size_t deglitch(const uint16_t* in, size_t n, uint16_t* out) {
    if (n > LIL_MAX_RAW_TIMINGS) n = LIL_MAX_RAW_TIMINGS;
    size_t count = 0;
    for (size_t i = 0; i < n; i++) out[count++] = in[i];

    size_t i = 1;
    while (i + 1 < count) {
        if (out[i] < GLITCH_THRESHOLD) {
            uint32_t merged = (uint32_t)out[i - 1] + out[i] + out[i + 1];
            out[i - 1] = (merged > 0xFFFFu) ? 0xFFFFu : (uint16_t)merged;
            memmove(&out[i], &out[i + 2], (count - i - 2) * sizeof(out[0]));
            count -= 2;
        } else {
            i++;
        }
    }
    return count;
}

/* Read at most 64 data bits from `pairs[start..end)`.
 *
 * `dur0_is_H = 1` means pairs[i].a is the HIGH half, .b is the LOW half;
 * flip for the other orientation. T is the base pulse length derived from
 * sync. sync_long_dur is the largest component of the sync pair — used to
 * detect end-of-frame (we stop if a pair's largest half is > sync_long/2).
 *
 * AGC compensation: the first bit after sync has its HIGH half truncated
 * to ~0.5T because the CC1101 AGC is still recovering. For that one bit
 * we match on the LOW half only. For subsequent bits we weight the HIGH
 * error 3× — the CC1101 slicer preserves HIGH timings but squashes LOW to
 * ~1T regardless of protocol.
 */
static void read_bits_run(
    const uint16_t* pairs_ab,
    size_t start, size_t end,
    int dur0_is_H,
    int T,
    uint16_t sync_long_dur,
    uint8_t zero_h, uint8_t zero_l,
    uint8_t one_h,  uint8_t one_l,
    uint64_t* out_key, int* out_bits)
{
    uint64_t key = 0;
    int bits = 0;

    /* bit_len is the max total duration a correct bit can have when weighted.
     * Same formula as the TS reference; stays constant for a given protocol
     * so hoist it out of the inner loop. */
    int max_zh = (zero_h > one_h) ? zero_h : one_h;
    int max_zl = (zero_l > one_l) ? zero_l : one_l;
    int bit_len = HIGH_W * T * max_zh + T * max_zl;
    int err_threshold = bit_len * BIT_THRESH_PCT / 100;
    int sync_stop = (int)sync_long_dur / 2;

    for (size_t i = start; i < end && bits < 64; i++) {
        uint16_t a = pairs_ab[i * 2];
        uint16_t b = pairs_ab[i * 2 + 1];
        uint16_t dH = dur0_is_H ? a : b;
        uint16_t dL = dur0_is_H ? b : a;

        if (dH == 0 || dL == 0) break;
        if (u16_max(dH, dL) > sync_stop) break;  /* hit the next sync */

        int err_zero, err_one;
        if (i == start && dH < T) {
            /* AGC recovery: use LOW only */
            int d = (int)dL;
            err_zero = abs(d - T * (int)zero_l);
            err_one  = abs(d - T * (int)one_l);
        } else {
            int dh = (int)dH;
            int dl = (int)dL;
            err_zero = HIGH_W * abs(dh - T * (int)zero_h) + abs(dl - T * (int)zero_l);
            err_one  = HIGH_W * abs(dh - T * (int)one_h)  + abs(dl - T * (int)one_l);
        }

        int min_err = (err_zero < err_one) ? err_zero : err_one;
        if (min_err > err_threshold) break;

        int bit = (err_one < err_zero) ? 1 : 0;
        key = (key << 1) | (uint64_t)bit;
        bits++;
    }

    *out_key = key;
    *out_bits = bits;
}

/* Try one (sync, protocol, orientation) combination. Returns 1 on match,
 * with outputs filled; 0 on reject. `after_sync` = 1 if bits came from the
 * post-sync region, 0 if from the pre-sync fallback. */
static int try_combination(
    const lil_rc_protocol_t* proto,
    const uint16_t* pairs_ab, size_t pair_count,
    size_t sync_idx,
    int dur0_is_H,
    uint64_t* out_key, int* out_bits,
    uint16_t* out_T, int* out_after_sync)
{
    uint16_t a = pairs_ab[sync_idx * 2];
    uint16_t b = pairs_ab[sync_idx * 2 + 1];
    uint16_t sync_H = dur0_is_H ? a : b;
    uint16_t sync_L = dur0_is_H ? b : a;

    int sync_sum = (int)proto->sync_h + (int)proto->sync_l;
    if (sync_sum == 0) return 0;

    int sync_long = (proto->sync_h > proto->sync_l) ? proto->sync_h : proto->sync_l;
    if (sync_long == 0) return 0;
    uint16_t sync_long_dur = u16_max(sync_H, sync_L);
    int T = (int)sync_long_dur / sync_long;
    if (T < MIN_T || T > MAX_T) return 0;

    /* Verify sync halves within SYNC_TOL_PCT ± 1T */
    int expected_H = T * (int)proto->sync_h;
    int expected_L = T * (int)proto->sync_l;
    int tol_H = expected_H * SYNC_TOL_PCT / 100 + T;
    int tol_L = expected_L * SYNC_TOL_PCT / 100 + T;
    if (abs((int)sync_H - expected_H) > tol_H) return 0;
    if (abs((int)sync_L - expected_L) > tol_L) return 0;

    /* Read post-sync first */
    uint64_t key = 0;
    int bits = 0;
    int after_sync = 1;
    if (sync_idx + 1 < pair_count) {
        read_bits_run(pairs_ab, sync_idx + 1, pair_count,
                      dur0_is_H, T, sync_long_dur,
                      proto->zero_h, proto->zero_l,
                      proto->one_h,  proto->one_l,
                      &key, &bits);
    }

    /* Pre-sync fallback for RMT buffers that captured only the last repeat.
     * PT2262 frames are [data][sync] so data precedes sync. */
    if (bits < 12 && sync_idx >= 4) {
        uint64_t before_key = 0;
        int before_bits = 0;
        read_bits_run(pairs_ab, 0, sync_idx,
                      dur0_is_H, T, sync_long_dur,
                      proto->zero_h, proto->zero_l,
                      proto->one_h,  proto->one_l,
                      &before_key, &before_bits);
        if (before_bits > bits) {
            key = before_key;
            bits = before_bits;
            after_sync = 0;
        }
    }

    if (bits < 4) return 0;
    if (key == 0) return 0;

    bits = pad_off_by_one(bits);

    *out_key = key;
    *out_bits = bits;
    *out_T = (uint16_t)T;
    *out_after_sync = after_sync;
    return 1;
}

static int score_result(int bits, int after_sync) {
    int std = is_std_bits(bits);
    int bonus = after_sync ? 50 : 0;
    return (std ? bits * 150 : bits * 50) + bonus + bits;
}

lil_status_t lil_decode_raw_timings(
    const uint16_t* durations, size_t count,
    uint8_t start_level, float freq,
    lil_signal_t* out)
{
    (void)start_level;  /* both orientations are tried; start_level is advisory */
    if (!out || !durations) return LIL_ERR_BAD_ARG;
    *out = (lil_signal_t){0};
    if (count < 6) return LIL_ERR_NO_MATCH;

    uint16_t clean[LIL_MAX_RAW_TIMINGS];
    size_t clean_count = deglitch(durations, count, clean);
    if (clean_count < 6) return LIL_ERR_NO_MATCH;

    /* Drop an odd trailing sample so every (H,L) pair is complete. */
    size_t pair_count = clean_count / 2;
    if (pair_count < 3) return LIL_ERR_NO_MATCH;

    /* Median for sync threshold = median × 4 */
    uint16_t sorted[LIL_MAX_RAW_TIMINGS];
    memcpy(sorted, clean, clean_count * sizeof(uint16_t));
    qsort(sorted, clean_count, sizeof(uint16_t), cmp_u16);
    uint32_t sync_threshold = (uint32_t)sorted[clean_count / 2] * 4;

    /* Sync candidates: any pair whose larger component exceeds threshold */
    size_t sync_cands[MAX_PAIRS];
    size_t sync_cand_count = 0;
    for (size_t i = 0; i < pair_count; i++) {
        uint16_t m = u16_max(clean[i * 2], clean[i * 2 + 1]);
        if ((uint32_t)m > sync_threshold) {
            sync_cands[sync_cand_count++] = i;
        }
    }
    if (sync_cand_count == 0) {
        /* Fallback: single pair with the max duration */
        uint16_t max_dur = 0;
        size_t max_idx = 0;
        for (size_t i = 0; i < pair_count; i++) {
            uint16_t m = u16_max(clean[i * 2], clean[i * 2 + 1]);
            if (m > max_dur) { max_dur = m; max_idx = i; }
        }
        sync_cands[0] = max_idx;
        sync_cand_count = 1;
    }

    /* Brute force every (sync, proto, orientation) triple */
    int best_score = 0;
    int have_best = 0;
    lil_signal_t best = {0};

    size_t proto_count = lil_protocol_count();
    for (size_t sc = 0; sc < sync_cand_count; sc++) {
        size_t sync_idx = sync_cands[sc];
        for (size_t p = 0; p < proto_count; p++) {
            const lil_rc_protocol_t* proto = lil_protocol_at(p);
            for (int ori = 0; ori < 2; ori++) {
                int dur0_is_H = (ori == 0);
                uint64_t key;
                int bits;
                uint16_t T;
                int after_sync;
                if (!try_combination(proto, clean, pair_count, sync_idx,
                                     dur0_is_H, &key, &bits, &T, &after_sync)) {
                    continue;
                }
                int score = score_result(bits, after_sync);
                if (score > best_score) {
                    best_score = score;
                    have_best = 1;
                    best.protocol     = proto->id;
                    best.key          = key;
                    best.bits         = (uint8_t)bits;
                    best.pulse_length = T;
                    best.freq         = freq;
                }
            }
        }
    }

    if (!have_best) return LIL_ERR_NO_MATCH;
    *out = best;
    return LIL_OK;
}
