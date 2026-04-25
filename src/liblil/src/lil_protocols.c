#include "lil.h"

/* RCSwitch protocol table — byte-for-byte mirror of:
 *   desktop/src/renderer/src/lib/constants.ts : RC_PROTOCOLS
 *
 * Each entry is { id, name, T, sync[h,l], zero[h,l], one[h,l], inverted }.
 * Multipliers are 1..N of the base pulse length T; the wire waveform is
 *   HIGH for (T * H) then LOW for (T * L).
 * `inverted` swaps HIGH/LOW on the wire (carrier-off = active).
 */
static const lil_rc_protocol_t k_protocols[] = {
    { 1,  "PT2262",       350,  1,  31,  1,  3,  3, 1, 0 },
    { 2,  "SC5262",       650,  1,  10,  1,  2,  2, 1, 0 },
    { 3,  "HX2262",       100, 30,  71,  4, 11,  9, 6, 0 },
    { 4,  "EV1527",       380,  1,   6,  1,  3,  3, 1, 0 },
    { 5,  "HT6P20B",      500,  6,  14,  1,  2,  2, 1, 0 },
    { 6,  "HT6P20B~",     450, 23,   1,  1,  2,  2, 1, 1 },
    { 7,  "HS2303-PT",    150,  2,  62,  1,  6,  6, 1, 0 },
    { 8,  "Conrad RX",    200,  3, 130,  7, 16,  3,16, 0 },
    { 9,  "Conrad TX",    200,130,   7, 16,  7, 16, 3, 1 },
    { 10, "1ByOne",       365, 18,   1,  3,  1,  1, 3, 1 },
    { 11, "HT12E",        270, 36,   1,  1,  2,  2, 1, 1 },
    { 12, "SM5212",       320, 36,   1,  1,  2,  2, 1, 1 },
    { 13, "Mumbi",        100,  3, 100,  3,  8,  8, 3, 0 },
    { 14, "Blyss",        500,  1,  14,  1,  3,  3, 1, 0 },
    { 15, "sc2260R4",     415,  1,  30,  1,  3,  4, 1, 0 },
    { 16, "HomeNetWerks", 250, 20,  10,  1,  1,  3, 1, 0 },
    { 17, "ORNO",          80,  3,  25,  3, 13, 11, 5, 0 },
    { 18, "CLARUS",        82,  2,  65,  3,  5,  7, 1, 0 },
    { 19, "NEC",          560, 16,   8,  1,  1,  1, 3, 0 },
    { 20, "CAME 12",      250,  1,   3,  2,  1,  1, 2, 0 },
    { 21, "FAAC",         330,  1,  34,  2,  1,  1, 2, 0 },
    { 22, "NICE",         700,  1,  36,  2,  1,  1, 2, 0 },
    { 23, "Protocol 23",  400,  0,  10,  2,  1,  1, 2, 0 }
};

static const size_t k_protocol_count = sizeof(k_protocols) / sizeof(k_protocols[0]);

size_t lil_protocol_count(void) {
    return k_protocol_count;
}

const lil_rc_protocol_t* lil_protocol_at(size_t idx) {
    if (idx >= k_protocol_count) return NULL;
    return &k_protocols[idx];
}

const lil_rc_protocol_t* lil_protocol_by_id(uint8_t id) {
    for (size_t i = 0; i < k_protocol_count; i++) {
        if (k_protocols[i].id == id) return &k_protocols[i];
    }
    return NULL;
}
