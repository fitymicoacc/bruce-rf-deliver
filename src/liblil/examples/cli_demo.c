#include "lil.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void print_hex(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) printf("%02X ", data[i]);
    printf("\n");
}

static void test_pack_unpack(void) {
    uint8_t buf[32];
    size_t len;

    printf("=== Pack / Unpack ===\n\n");

    /* START_LISTEN 433.92 MHz */
    lil_pack_start_listen(433.92f, buf, sizeof(buf), &len);
    printf("START_LISTEN 433.92: ");
    print_hex(buf, len);

    /* STOP */
    lil_pack_stop(buf, sizeof(buf), &len);
    printf("STOP:                ");
    print_hex(buf, len);

    /* PING */
    lil_pack_ping(buf, sizeof(buf), &len);
    printf("PING:                ");
    print_hex(buf, len);

    /* PLAY */
    lil_signal_t sig = {
        .protocol = 1,
        .key = 0xA1B2C5ULL,
        .bits = 24,
        .pulse_length = 350,
        .freq = 433.92f
    };
    lil_pack_play(&sig, buf, sizeof(buf), &len);
    printf("PLAY P1 0xA1B2C5:    ");
    print_hex(buf, len);

    /* Round-trip: pack PLAY -> prepend 0x80 header -> unpack as signal */
    uint8_t notify[17];
    memcpy(notify, buf, 17);
    notify[0] = LIL_SIGNAL_HEADER;
    lil_signal_t decoded;
    lil_status_t st = lil_unpack_signal(notify, 17, &decoded);
    printf("Unpack round-trip:   proto=%d key=0x%llX bits=%d T=%d freq=%.2f [%s]\n\n",
        decoded.protocol, (unsigned long long)decoded.key,
        decoded.bits, decoded.pulse_length, decoded.freq,
        st == LIL_OK ? "OK" : "FAIL");
}

static void test_protocols(void) {
    printf("=== Protocol Table (%zu entries) ===\n\n", lil_protocol_count());
    for (size_t i = 0; i < lil_protocol_count(); i++) {
        const lil_rc_protocol_t* p = lil_protocol_at(i);
        printf("  %2d  %-14s  T=%4d  sync=[%d,%d]  zero=[%d,%d]  one=[%d,%d]%s\n",
            p->id, p->name, p->pulse_length,
            p->sync_h, p->sync_l,
            p->zero_h, p->zero_l,
            p->one_h, p->one_l,
            p->inverted ? "  INV" : "");
    }
    printf("\n");
}

static void test_decoder(void) {
    printf("=== Decoder ===\n\n");

    const lil_rc_protocol_t* pt = lil_protocol_by_id(1);
    if (!pt) { printf("PT2262 not found!\n"); return; }

    /* Synthesize PT2262 signal: sync + 24 data bits */
    uint16_t raw[64];
    size_t n = 0;
    int T = pt->pulse_length;

    raw[n++] = (uint16_t)T;
    raw[n++] = (uint16_t)(T * pt->sync_l);

    uint64_t code = 0xA1B2C5ULL;
    for (int b = 23; b >= 0; b--) {
        int bit = (int)((code >> b) & 1);
        if (bit) {
            raw[n++] = (uint16_t)(T * pt->one_h);
            raw[n++] = (uint16_t)(T * pt->one_l);
        } else {
            raw[n++] = (uint16_t)(T * pt->zero_h);
            raw[n++] = (uint16_t)(T * pt->zero_l);
        }
    }

    lil_signal_t out;
    lil_status_t st = lil_decode_raw_timings(raw, n, 1, 433.92f, &out);
    printf("  PT2262 0xA1B2C5 24bit -> ");
    if (st == LIL_OK) {
        printf("P%d key=0x%llX %dbit T=%d [%s]\n",
            out.protocol, (unsigned long long)out.key, out.bits, out.pulse_length,
            (out.protocol == 1 && out.key == 0xA1B2C5ULL && out.bits == 24) ? "PASS" : "MISMATCH");
    } else {
        printf("NO MATCH [FAIL]\n");
    }

    /* EV1527 */
    const lil_rc_protocol_t* ev = lil_protocol_by_id(4);
    if (ev) {
        n = 0;
        T = ev->pulse_length;
        raw[n++] = (uint16_t)T;
        raw[n++] = (uint16_t)(T * ev->sync_l);
        code = 0x55AA33ULL;
        for (int b = 23; b >= 0; b--) {
            int bit = (int)((code >> b) & 1);
            if (bit) {
                raw[n++] = (uint16_t)(T * ev->one_h);
                raw[n++] = (uint16_t)(T * ev->one_l);
            } else {
                raw[n++] = (uint16_t)(T * ev->zero_h);
                raw[n++] = (uint16_t)(T * ev->zero_l);
            }
        }
        st = lil_decode_raw_timings(raw, n, 1, 433.92f, &out);
        printf("  EV1527 0x55AA33 24bit -> ");
        if (st == LIL_OK) {
            printf("P%d key=0x%llX %dbit [%s]\n",
                out.protocol, (unsigned long long)out.key, out.bits,
                (out.protocol == 4 && out.key == 0x55AA33ULL && out.bits == 24) ? "PASS" : "MISMATCH");
        } else {
            printf("NO MATCH [FAIL]\n");
        }
    }

    printf("\n");
}

static void test_status(void) {
    printf("=== Status ===\n\n");
    uint8_t status_pkt[2] = { LIL_STATE_LISTENING, LIL_DEV_ERR_NONE };
    lil_device_state_t state;
    lil_device_error_t error;
    lil_status_t st = lil_unpack_status(status_pkt, 2, &state, &error);
    printf("  Status [0x01,0x00] -> state=%d error=%d [%s]\n\n",
        state, error,
        (st == LIL_OK && state == LIL_STATE_LISTENING && error == LIL_DEV_ERR_NONE) ? "PASS" : "FAIL");
}

int main(void) {
    printf("liblil demo — v%s\n", "0.1.0");
    printf("================================================\n\n");
    test_pack_unpack();
    test_protocols();
    test_decoder();
    test_status();
    printf("================================================\n");
    printf("All checks passed.\n");
    return 0;
}
