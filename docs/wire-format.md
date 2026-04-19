# Lil wire format

Single source of truth for the BLE GATT protocol spoken between a
Bruce-RF peripheral (or `mock-bruce/` emulator) and any client
(`lil-kotlin`, `desktop/`, third-party apps). Every implementation in
this tree **must** match this file byte-for-byte.

The canonical implementations:

| language | file |
|----------|------|
| C++ (firmware)   | `bruce-rf/src/modules/ble_rf/ble_rf_service.h`                       |
| C  (lib)         | `liblil/include/lil.h` + `liblil/src/lil_wire.c`                     |
| Kotlin (KMP)     | `lil-kotlin/library/src/commonMain/kotlin/ru/pluttan/lil/{Uuids,Protocol}.kt` |
| TypeScript (app) | `desktop/src/renderer/src/lib/{constants,protocol}.ts`               |
| Python (mock)    | `mock-bruce/mock_bruce.py`                                           |

A script `scripts/verify-wire-identity.sh` grep-checks UUIDs, command
IDs, and notify headers across all five sources.

All multi-byte fields are **little-endian**. `float32` is IEEE 754 as
stored by memcpy on every target host we care about (x86_64, arm,
aarch64).

---

## GATT layout

```
Service   12345678-1234-5678-1234-56789abcdef0
│
├── Characteristic CMD      12345678-1234-5678-1234-56789abcdef1  (write / write-without-response)
├── Characteristic SIGNAL   12345678-1234-5678-1234-56789abcdef2  (notify)
└── Characteristic STATUS   12345678-1234-5678-1234-56789abcdef3  (read + notify)
```

The peripheral advertises with `LocalName = "Bruce-RF"` and the above
service UUID.

---

## Commands (host → device, written to CMD)

Every command starts with a single-byte ID. Payloads follow.

### 0x01 START_LISTEN — 5 bytes

| offset | type    | meaning        |
|--------|---------|----------------|
| 0      | u8      | 0x01           |
| 1..4   | f32 LE  | frequency MHz  |

Moves the device from IDLE to LISTENING on the given frequency. The
device emits SIGNAL notifications as frames arrive.

### 0x02 STOP — 1 byte

| offset | type | meaning |
|--------|------|---------|
| 0      | u8   | 0x02    |

Stops LISTENING / SCANNING / TRANSMITTING, returns to IDLE.

### 0x03 PLAY — 17 bytes

| offset | type    | meaning                      |
|--------|---------|------------------------------|
| 0      | u8      | 0x03                         |
| 1      | u8      | RCSwitch protocol id (1..23) |
| 2..9   | u64 LE  | signal code (MSB-first bits) |
| 10     | u8      | data bits in code (0..64)    |
| 11..12 | u16 LE  | base T in microseconds       |
| 13..16 | f32 LE  | frequency MHz                |

Transmits the signal once on the given frequency. The device momentarily
moves to TRANSMITTING then back to IDLE.

### 0x04 PING — 1 byte

| offset | type | meaning |
|--------|------|---------|
| 0      | u8   | 0x04    |

Keepalive. The device replies with a STATUS notification carrying the
current state.

### 0x05 SCAN_RANGES — variable

| offset                 | type   | meaning                   |
|------------------------|--------|---------------------------|
| 0                      | u8     | 0x05                      |
| 1..2                   | u16 LE | dwell ms per frequency    |
| 3                      | u8     | range count N (max 8)     |
| 4+N·12..               | f32×3  | per range: start, end, step (all MHz) |

Total size = 4 + N·12 bytes. Device scans every frequency in the
supplied ranges, emitting SIGNAL notifications for hits.

### 0x06 SCAN_LIST — variable

| offset      | type   | meaning                       |
|-------------|--------|-------------------------------|
| 0           | u8     | 0x06                          |
| 1..2        | u16 LE | dwell ms per frequency        |
| 3           | u8     | freq count N (max 32)         |
| 4..4+N·4    | f32 LE | N frequencies in MHz          |

Total size = 4 + N·4 bytes.

---

## Notifications (device → host)

### CHAR_SIGNAL — 0x80 decoded signal, 17 bytes

| offset | type    | meaning                      |
|--------|---------|------------------------------|
| 0      | u8      | 0x80                         |
| 1      | u8      | RCSwitch protocol id         |
| 2..9   | u64 LE  | signal code                  |
| 10     | u8      | data bits                    |
| 11..12 | u16 LE  | base T microseconds          |
| 13..16 | f32 LE  | frequency MHz                |

Layout identical to the PLAY body (bytes 1..16), only the leading byte
differs. A client can round-trip a captured signal as PLAY by copying
the body and setting byte 0 to 0x03.

### CHAR_SIGNAL — 0x81 raw timings, variable

| offset            | type   | meaning                       |
|-------------------|--------|-------------------------------|
| 0                 | u8     | 0x81                          |
| 1..4              | f32 LE | frequency MHz                 |
| 5                 | u8     | pulse count N                 |
| 6                 | u8     | start level (0 = LOW, 1 = HIGH) |
| 7..7+N·2          | u16 LE | N durations in microseconds   |

Header is 7 bytes, body is `N·2` bytes. Durations alternate HIGH/LOW
starting from `start level`. Firmware sends 0x81 when RCSwitch on the
device can't match a protocol — the client is expected to either decode
with `liblil` (C) or `KotlinDecoder` (pure-Kotlin).

**Firmware invariant**: the first byte of the durations array is a
fabricated 1T HIGH pulse, prepended before the real sync LOW that
RCSwitch captured. See `bruce-rf/src/modules/ble_rf/ble_rf_capture.cpp`
`pollRx()` — this compensates for the fact that RCSwitch only retains
the LOW side of the sync edge.

### CHAR_STATUS — 2 bytes, read + notify

| offset | type | meaning              |
|--------|------|----------------------|
| 0      | u8   | device state         |
| 1      | u8   | last error (0=none)  |

#### Device states

| byte | symbol       |
|------|--------------|
| 0x00 | IDLE         |
| 0x01 | LISTENING    |
| 0x02 | TRANSMITTING |
| 0x03 | SCANNING     |
| 0xFF | ERROR        |

#### Error codes

| byte | symbol            |
|------|-------------------|
| 0x00 | NONE              |
| 0x01 | CC1101_FAIL       |
| 0x02 | INVALID_CMD       |
| 0x03 | TX_FAIL           |

Unknown error values from future firmware versions should be treated as
`NONE` by backward-compatible clients.

---

## Changelog

- **2026-04-23** — protocol frozen at this shape for the lil-kotlin +
  liblil + mock-bruce scaffold. No prior versions beyond the first
  freelance iteration on the device (same UUIDs, same bytes).
