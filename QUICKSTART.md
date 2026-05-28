# Quick Start

Пошаговая инструкция по использованию всех компонентов.

---

## 1. Прошивка устройства

### LilyGO T-Embed CC1101 (основная прошивка)

```bash
pip install esptool
esptool.py --chip esp32s3 write_flash 0x0 firmware/Bruce-lilygo-t-embed-cc1101.bin
```

После прошивки устройство сразу начинает BLE-рассылку как "Bruce-RF".
Вход в меню не требуется — все команды работают через BLE.

### ESP32-C3 + CC1101 (тестовый стенд)

```bash
esptool.py --chip esp32c3 write_flash 0x0 firmware/mini-bruce-c3.bin
```

Схема подключения CC1101 к ESP32-C3 Super Mini:
```
CC1101    ESP32-C3
VCC  ---> 3.3V
GND  ---> GND
SCK  ---> GPIO3
MOSI ---> GPIO10
MISO ---> GPIO20
CS   ---> GPIO4
GDO0 ---> GPIO7
```

LED-индикация:
- Двойное моргание — ожидание BLE
- Горит постоянно — подключен
- Медленное моргание — приём (LISTENING)
- Быстрое моргание — передача (TX)

Deep sleep: долгое нажатие BOOT (>2 сек) или 60 сек без BLE.
Пробуждение: короткое нажатие BOOT (нужна перемычка GPIO9 -> GPIO0).

---

## 2. Подключение по BLE

Устройство advertise'ит себя как "Bruce-RF".

BLE GATT:
```
Service:  12345678-1234-5678-1234-56789abcdef0

  CMD     (write):      12345678-1234-5678-1234-56789abcdef1
  SIGNAL  (notify):     12345678-1234-5678-1234-56789abcdef2
  STATUS  (read+notify): 12345678-1234-5678-1234-56789abcdef3
```

### Быстрый тест через nRF Connect

1. Открыть nRF Connect на телефоне
2. Найти "Bruce-RF", подключиться
3. Подписаться на SIGNAL и STATUS (включить notify)
4. В CMD написать `04` (PING) → STATUS ответит `00 00` (IDLE)
5. В CMD написать `01 C3F5D843` (START_LISTEN 433.92 МГц) → STATUS `01 00` (LISTENING)
6. Нажать 433 МГц пульт рядом → SIGNAL notify придёт
7. В CMD написать `02` (STOP) → STATUS `00 00` (IDLE)

---

## 3. Формат команд

Все числа little-endian.

### START_LISTEN (0x01) — 5 байт
```
01 [freq: float32 LE]
Пример: 01 C3F5D843  → 433.92 МГц
```

### STOP (0x02) — 1 байт
```
02
```

### PLAY (0x03) — 17 байт
```
03 [protocol: u8] [key: u64 LE] [bits: u8] [pulse: u16 LE] [freq: f32 LE]
Пример: 03 01 C5B2A10000000000 18 5E01 C3F5D843
  → Protocol 1 (PT2262), key=0xA1B2C5, 24 бита, T=350мкс, 433.92 МГц
```

### PING (0x04) — 1 байт
```
04
→ Ответ: STATUS notify с текущим состоянием
```

### SCAN_LIST (0x06) — переменная длина
```
06 [dwell_ms: u16 LE] [count: u8] [freq1: f32 LE] [freq2: f32 LE] ...
```

### SCAN_RANGES (0x05) — переменная длина
```
05 [dwell_ms: u16 LE] [count: u8] [start: f32] [end: f32] [step: f32] ...
```

---

## 4. Формат ответов (notify)

### SIGNAL 0x80 — декодированный сигнал (17 байт)
```
80 [proto: u8] [key: u64 LE] [bits: u8] [pulse: u16 LE] [freq: f32 LE]
```
Можно отправить обратно как PLAY (заменить 80 → 03).

### SIGNAL 0x81 — сырые тайминги (переменная)
```
81 [freq: f32 LE] [count: u8] [start_level: u8] [durations: u16 LE × count]
```
Декодировать через `lil_decode_raw_timings()` (C) или `KotlinDecoder` (Kotlin).

### STATUS — 2 байта
```
[state: u8] [error: u8]

state: 00=IDLE, 01=LISTENING, 02=TRANSMITTING, 03=SCANNING, FF=ERROR
error: 00=NONE, 01=CC1101_FAIL, 02=INVALID_CMD, 03=TX_FAIL
```

---

## 5. Использование C-библиотеки (liblil)

### Подключение

```c
#include "lil.h"
```

Линковка:
- Linux: `-llil` или `liblil.a`
- Windows: `liblil.dll`
- Android: `liblil-android-arm64.so` (через JNI или FFI)
- iOS: `liblil.xcframework`

### Примеры

```c
// Сформировать команду START_LISTEN
uint8_t buf[32];
size_t len;
lil_pack_start_listen(433.92f, buf, sizeof(buf), &len);
// buf[0..len-1] записать в BLE CMD

// Разобрать полученный 0x80 notify
lil_signal_t sig;
if (lil_unpack_signal(ble_data, ble_len, &sig) == LIL_OK) {
    printf("P%d key=0x%llX %dbit\n", sig.protocol, sig.key, sig.bits);
}

// Воспроизвести сигнал
lil_pack_play(&sig, buf, sizeof(buf), &len);
// buf → записать в BLE CMD

// Декодировать 0x81 raw timings
lil_raw_timings_t raw;
lil_unpack_raw_timings(ble_data, ble_len, &raw);
lil_signal_t decoded;
if (lil_decode_raw_timings(raw.durations, raw.count,
        raw.start_level, raw.freq, &decoded) == LIL_OK) {
    printf("Decoded: P%d 0x%llX\n", decoded.protocol, decoded.key);
}
```

### Проверка
```bash
./test-apps/cli_demo-linux     # Linux
./test-apps/cli_demo-windows.exe  # Windows
```

---

## 6. Использование Kotlin Multiplatform (lil-kotlin)

### Подключение в KMP-проекте

В `build.gradle.kts`:
```kotlin
dependencies {
    implementation(project(":library"))  // или как maven-зависимость
}
```

### Пример использования

```kotlin
val device = LilDevice(peripheral, scope)
device.connect()

// Подписка на сигналы
scope.launch {
    device.signals.collect { notification ->
        when (notification) {
            is LilDevice.Notification.Decoded -> {
                val sig = notification.signal
                println("P${sig.protocol} key=0x${sig.key.toString(16)} ${sig.bits}bit")
            }
            is LilDevice.Notification.Raw -> {
                val decoded = KotlinDecoder.decode(notification.raw)
                if (decoded != null) println("Decoded: $decoded")
            }
        }
    }
}

// Начать слушать
device.startListen(433.92f)

// Воспроизвести
device.play(signal)

// Остановить
device.stopListen()
```

### Android Sample App

Установить `lil-kotlin/sample-android-debug.apk` на телефон.
3 экрана: Scan → Signals → Detail.

---

## 7. Протоколы

23 поддерживаемых RC-протокола:

| ID | Название | T (мкс) | Биты |
|----|----------|---------|------|
| 1 | PT2262 | 350 | 24 |
| 2 | SC5262 | 650 | 24 |
| 3 | HX2262 | 100 | 24 |
| 4 | EV1527 | 380 | 24 |
| 5 | HT6P20B | 500 | 24 |
| 6 | HT6P20B~ | 450 | 24 |
| 7 | HS2303-PT | 150 | 24 |
| 8 | Conrad RX | 200 | 24 |
| 9 | Conrad TX | 200 | 24 |
| 10 | 1ByOne | 365 | 24 |
| 11 | HT12E | 270 | 12 |
| 12 | SM5212 | 320 | 12 |
| 13 | Mumbi | 100 | 24 |
| 14 | Blyss | 500 | 24 |
| 15 | sc2260R4 | 415 | 24 |
| 16 | HomeNetWerks | 250 | 24 |
| 17 | ORNO | 80 | 24 |
| 18 | CLARUS | 82 | 24 |
| 19 | NEC | 560 | 32 |
| 20 | CAME 12 | 250 | 12 |
| 21 | FAAC | 330 | 24 |
| 22 | NICE | 700 | 24 |
| 23 | Protocol 23 | 400 | 24 |

Ролл-коды не поддерживаются.

---

## 8. Структура файлов

```
deliver/
├── firmware/
│   ├── Bruce-lilygo-t-embed-cc1101.bin   ← прошивка для T-Embed
│   └── mini-bruce-c3.bin                 ← прошивка для ESP32-C3
├── liblil/
│   ├── lil.h                             ← C API заголовок
│   ├── liblil-linux-x86_64.a / .so
│   ├── liblil-windows-x86_64.dll
│   ├── liblil-android-arm64.so           ← с JNI
│   └── liblil.xcframework/              ← iOS
├── lil-kotlin/
│   └── sample-android-debug.apk
├── test-apps/
│   ├── cli_demo-linux
│   └── cli_demo-windows.exe
├── src/                                  ← исходники
│   ├── liblil/                           ← C-библиотека
│   ├── lil-kotlin/                       ← KMP модуль
│   └── firmware/                         ← прошивки
└── docs/
    ├── API.md                            ← справочник liblil
    └── wire-format.md                    ← спецификация протокола
```
