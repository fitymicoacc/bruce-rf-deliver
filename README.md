# Deliverables — заказ 2

## Состав

### firmware/
- `Bruce-lilygo-t-embed-cc1101.bin` — прошивка Bruce-RF для LilyGO T-Embed CC1101. Авто-BLE advertise при включении + фоновая обработка команд (Ver.0.2). Прошивать через esptool: `esptool.py --chip esp32s3 write_flash 0x0 Bruce-lilygo-t-embed-cc1101.bin`
- `mini-bruce-c3.bin` — тестовая прошивка Mini-Bruce для ESP32-C3 + CC1101. Тот же BLE-протокол, LED-индикация, deep sleep. Прошивать: `esptool.py --chip esp32c3 write_flash 0x0 mini-bruce-c3.bin`

### liblil/
C-библиотека протокола Lil (wire-format + RCSwitch decoder).

- `lil.h` — заголовочный файл, extern "C" API
- `liblil-linux-x86_64.a` / `.so` — Linux static + shared
- `liblil-windows-x86_64.dll` — Windows shared (MinGW)
- `liblil-android-arm64.so` — Android arm64 + JNI
- `liblil.xcframework/` — iOS (device arm64 + simulator arm64/x86_64)

### lil-kotlin/
- `sample-android-debug.apk` — Android sample app (Compose Material3, BLE через Kable). Scan, Signals, Detail экраны.

### test-apps/
- `cli_demo-linux` — консольное тестовое приложение (Linux x86_64). Проверяет pack/unpack, decoder, protocols.
- `cli_demo-windows.exe` — то же для Windows x86_64.

### docs/
- `API.md` — справочник по liblil API (типы, функции, примеры)
- `wire-format.md` — спецификация BLE GATT протокола (байт-по-байт)

## Исходники

Все исходники находятся в соответствующих директориях проекта:

| Компонент | Путь |
|---|---|
| Прошивка Bruce-RF | `bruce-rf/src/modules/ble_rf/` |
| Прошивка Mini-Bruce (C3) | `src/main_ble.c` + `components/ble_service/` |
| liblil (C) | `liblil/` |
| lil-kotlin (KMP) | `lil-kotlin/` |
| Тестовые приложения | `liblil/examples/` |

## BLE-протокол (кратко)

```
Service:  12345678-1234-5678-1234-56789abcdef0
CMD:      ...def1  (write)     — команды устройству
SIGNAL:   ...def2  (notify)    — захваченные сигналы
STATUS:   ...def3  (read+notify) — состояние устройства
```

Команды: START_LISTEN (0x01), STOP (0x02), PLAY (0x03), PING (0x04), SCAN_RANGES (0x05), SCAN_LIST (0x06).

Подробности — `docs/wire-format.md`.
