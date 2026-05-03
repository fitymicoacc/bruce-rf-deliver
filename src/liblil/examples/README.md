# liblil examples

Тестовые мини-приложения для проверки liblil на каждой платформе.

## cli_demo (Linux / Windows / macOS)

Консольное C-приложение. Проверяет pack/unpack, таблицу протоколов,
декодер и status.

### Linux

```bash
cd liblil
gcc -std=c11 -O2 -Iinclude examples/cli_demo.c src/lil_*.c -lm -o cli_demo
./cli_demo
```

### Windows (кросс-компиляция через MinGW)

```bash
cd liblil
x86_64-w64-mingw32-gcc -std=c11 -O2 -Iinclude examples/cli_demo.c src/lil_*.c -lm -o cli_demo.exe
# запустить на Windows: cli_demo.exe
```

### macOS

```bash
cd liblil
clang -std=c11 -O2 -Iinclude examples/cli_demo.c src/lil_*.c -lm -o cli_demo
./cli_demo
```

### С использованием .so / .dll

```bash
# Linux (shared library)
gcc -std=c11 -O2 -Iinclude examples/cli_demo.c -Lbuild -llil -lm -o cli_demo
LD_LIBRARY_PATH=build ./cli_demo

# Windows (DLL)
x86_64-w64-mingw32-gcc -std=c11 -O2 -Iinclude examples/cli_demo.c -Lbuild-win -llil -o cli_demo.exe
```

## ios-demo (iOS / macOS Swift)

Swift Package, использует liblil через C-модуль.

Требует: Xcode, собранный `liblil.a` (через `scripts/build-ios.sh`
или вручную через `xcrun clang`).

```bash
cd liblil/examples/ios-demo
# Скомпилировать liblil.a и положить рядом, затем:
swift build -Xlinker -L../../build -Xlinker -llil
swift run LilDemo
```

Для Xcode-проекта: добавить `liblil.xcframework` из `dist/ios/`
как зависимость и импортировать `CLil`.

## Android

Sample Android app находится в `lil-kotlin/sample-android/`.
Compose Material3, 3 экрана (Scan, Signals, Detail).

```bash
cd lil-kotlin
gradle :sample-android:assembleDebug
# APK: sample-android/build/outputs/apk/debug/sample-android-debug.apk
```
