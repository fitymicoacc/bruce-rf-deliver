# liblil API Reference

C-библиотека протокола Lil для взаимодействия с Bruce-RF устройством
(ESP32-S3 + CC1101) через BLE GATT.

Заголовочный файл: `include/lil.h`

## Подключение

```c
#include "lil.h"
```

Сборка:
```bash
cmake -B build && cmake --build build
```

Результат: `liblil.a` (static), `liblil.so` / `liblil.dll` (shared).

---

## Типы

### lil_status_t

Коды возврата всех функций.

| Значение | Описание |
|---|---|
| `LIL_OK` (0) | Успешно |
| `LIL_ERR_BUF_TOO_SMALL` (-1) | Буфер слишком мал, `*out_len` содержит необходимый размер |
| `LIL_ERR_BAD_FRAME` (-2) | Некорректный пакет (неверный заголовок или длина) |
| `LIL_ERR_BAD_ARG` (-3) | NULL-указатель или невалидный аргумент |
| `LIL_ERR_NO_MATCH` (-4) | Декодер не нашёл совпадений |

### lil_signal_t

Декодированный RF-сигнал. Соответствует 0x80 notification payload
и телу команды PLAY.

```c
typedef struct {
    uint8_t  protocol;      // RCSwitch protocol id (1..23), 0 = unknown
    uint64_t key;           // код сигнала, LSB = последний бит
    uint8_t  bits;          // количество бит в key
    uint16_t pulse_length;  // базовый период T, микросекунды
    float    freq;          // частота, МГц
} lil_signal_t;
```

### lil_raw_timings_t

Сырые тайминги с CC1101/RMT (заголовок 0x81). Вход для декодера.

```c
typedef struct {
    float    freq;           // частота, МГц
    uint8_t  start_level;    // 0 = LOW, 1 = HIGH
    uint8_t  count;          // количество замеров
    uint16_t durations[256]; // микросекунды, LOW/HIGH чередуются
} lil_raw_timings_t;
```

### lil_freq_range_t

Диапазон частот для SCAN_RANGES.

```c
typedef struct {
    float start;  // МГц
    float end;    // МГц
    float step;   // МГц
} lil_freq_range_t;
```

### lil_rc_protocol_t

Описание RC-протокола из таблицы.

```c
typedef struct {
    uint8_t     id;            // 1..23
    const char* name;          // "PT2262", "EV1527" и т.д.
    uint16_t    pulse_length;  // базовый T, микросекунды
    uint8_t     sync_h, sync_l;
    uint8_t     zero_h, zero_l;
    uint8_t     one_h, one_l;
    uint8_t     inverted;      // 0 или 1
} lil_rc_protocol_t;
```

---

## Таблица протоколов

23 встроенных RC-протокола (PT2262, SC5262, HX2262, EV1527, HT6P20B,
HS2303-PT, Conrad, 1ByOne, HT12E, SM5212, Mumbi, Blyss, sc2260R4,
HomeNetWerks, ORNO, CLARUS, NEC, CAME 12, FAAC, NICE, Protocol 23).

```c
size_t lil_protocol_count(void);
const lil_rc_protocol_t* lil_protocol_at(size_t idx);
const lil_rc_protocol_t* lil_protocol_by_id(uint8_t id);
```

---

## Pack-функции (host -> устройство)

Все pack-функции записывают в `buf` размером `buf_len` байт.
`*out_len` получает количество записанных (или необходимых) байт.

### lil_pack_start_listen

```c
lil_status_t lil_pack_start_listen(float freq,
    uint8_t* buf, size_t buf_len, size_t* out_len);
```

5 байт: `0x01` + `freq` (float32 LE).

### lil_pack_stop

```c
lil_status_t lil_pack_stop(uint8_t* buf, size_t buf_len, size_t* out_len);
```

1 байт: `0x02`.

### lil_pack_ping

```c
lil_status_t lil_pack_ping(uint8_t* buf, size_t buf_len, size_t* out_len);
```

1 байт: `0x04`.

### lil_pack_play

```c
lil_status_t lil_pack_play(const lil_signal_t* sig,
    uint8_t* buf, size_t buf_len, size_t* out_len);
```

17 байт: `0x03` + protocol(1) + key(u64 LE) + bits(1) + pulse(u16 LE) + freq(f32 LE).

### lil_pack_scan_ranges

```c
lil_status_t lil_pack_scan_ranges(const lil_freq_range_t* ranges,
    uint8_t count, uint16_t dwell_ms,
    uint8_t* buf, size_t buf_len, size_t* out_len);
```

Переменная длина: `0x05` + dwell(u16 LE) + count(1) + N * (start + end + step, f32 LE).

### lil_pack_scan_list

```c
lil_status_t lil_pack_scan_list(const float* freqs, uint8_t count,
    uint16_t dwell_ms,
    uint8_t* buf, size_t buf_len, size_t* out_len);
```

Переменная длина: `0x06` + dwell(u16 LE) + count(1) + N * freq(f32 LE).

---

## Unpack-функции (устройство -> host)

### lil_unpack_signal

```c
lil_status_t lil_unpack_signal(const uint8_t* data, size_t len,
    lil_signal_t* out);
```

Парсит 17-байтный 0x80 notification. Возвращает `LIL_ERR_BAD_FRAME`
при неверном заголовке или недостаточной длине.

### lil_unpack_raw_timings

```c
lil_status_t lil_unpack_raw_timings(const uint8_t* data, size_t len,
    lil_raw_timings_t* out);
```

Парсит 0x81 notification (7+ байт). Тайминги обрезаются до 256 максимум.

### lil_unpack_status

```c
lil_status_t lil_unpack_status(const uint8_t* data, size_t len,
    lil_device_state_t* state, lil_device_error_t* error);
```

Парсит 2-байтный STATUS payload.

---

## Декодер

```c
lil_status_t lil_decode_raw_timings(const uint16_t* durations,
    size_t count, uint8_t start_level, float freq,
    lil_signal_t* out);
```

Brute-force декодер сырых таймингов CC1101/RMT.

**Алгоритм:**
1. Deglitch: пульсы < 30 мкс сливаются с соседями.
2. Sync candidates: пары с компонентой > median * 4.
3. Перебор: каждый sync * 23 протокола * 2 ориентации.
4. AGC-компенсация первого бита после sync (HIGH взвешен x3).
5. Scoring: стандартные длины (8/12/16/20/24/28/32/40/48/64 бит)
   получают 150 очков/бит, нестандартные 50.

Возвращает `LIL_OK` + заполненный `out` при совпадении,
`LIL_ERR_NO_MATCH` если ничего не подошло.

---

## Константы

```c
#define LIL_SERVICE_UUID      "12345678-1234-5678-1234-56789abcdef0"
#define LIL_CMD_CHAR_UUID     "12345678-1234-5678-1234-56789abcdef1"
#define LIL_SIGNAL_CHAR_UUID  "12345678-1234-5678-1234-56789abcdef2"
#define LIL_STATUS_CHAR_UUID  "12345678-1234-5678-1234-56789abcdef3"

#define LIL_SIGNAL_HEADER       0x80
#define LIL_RAW_TIMINGS_HEADER  0x81
#define LIL_SIGNAL_PACKET_SIZE  17
#define LIL_STATUS_PACKET_SIZE  2
#define LIL_MAX_SCAN_FREQS      32
#define LIL_MAX_SCAN_RANGES     8
#define LIL_MAX_RAW_TIMINGS     256
```

## Пример использования

```c
#include "lil.h"
#include <stdio.h>

// Отправить START_LISTEN на 433.92 МГц
uint8_t buf[32];
size_t len;
lil_pack_start_listen(433.92f, buf, sizeof(buf), &len);
// buf[0..len-1] -> записать в BLE CMD characteristic

// Получить 0x80 notification и распарсить
lil_signal_t sig;
if (lil_unpack_signal(ble_data, ble_len, &sig) == LIL_OK) {
    printf("Protocol %d, key=0x%llX, %d bits, %.2f MHz\n",
        sig.protocol, sig.key, sig.bits, sig.freq);

    // Воспроизвести тот же сигнал
    lil_pack_play(&sig, buf, sizeof(buf), &len);
    // buf -> записать в BLE CMD characteristic
}

// Декодировать 0x81 raw timings
lil_raw_timings_t raw;
if (lil_unpack_raw_timings(ble_data, ble_len, &raw) == LIL_OK) {
    lil_signal_t decoded;
    if (lil_decode_raw_timings(raw.durations, raw.count,
            raw.start_level, raw.freq, &decoded) == LIL_OK) {
        printf("Decoded: P%d 0x%llX\n", decoded.protocol, decoded.key);
    }
}
```
