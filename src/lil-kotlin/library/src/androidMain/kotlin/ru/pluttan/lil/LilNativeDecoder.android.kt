package ru.pluttan.lil

/**
 * Android actual: forwards to `liblil` via JNI. The native library is
 * expected to live under `jniLibs/<abi>/liblil.so` inside the consuming
 * AAR/APK; the JNI entry point
 * `Java_ru_pluttan_lil_LilNativeDecoder_nativeDecode` is provided by
 * `liblil/src/jni/lil_jni.c` when liblil is built with
 * `-DLIBLIL_BUILD_JNI=ON` (the script `liblil/scripts/build-android.sh`
 * passes that flag).
 *
 * Return layout (5 longs, defined in `lil_jni.c`):
 *   [0] protocol, [1] key (full 64 bits), [2] bits,
 *   [3] pulse_length, [4] freq raw bits — `Float.fromBits(...)` on the
 *   Kotlin side.
 *
 * If the `.so` is missing, decode falls through to [KotlinDecoder] so the
 * library remains usable end-to-end.
 */
actual object LilNativeDecoder {
    @Volatile private var linked = false
    init {
        runCatching {
            System.loadLibrary("lil")
            linked = true
        }
    }

    private external fun nativeDecode(
        durations: ShortArray,
        startLevel: Int,
        freqMHz: Float
    ): LongArray?

    actual fun decode(raw: LilRawTimings): LilSignal? {
        if (!linked) return KotlinDecoder.decode(raw)
        val shorts = ShortArray(raw.durations.size) { raw.durations[it].toShort() }
        val arr = runCatching { nativeDecode(shorts, raw.startLevel, raw.freqMHz) }
            .getOrNull() ?: return null
        return LilSignal(
            protocol    = arr[0].toInt(),
            key         = arr[1].toULong(),
            bits        = arr[2].toInt(),
            pulseLength = arr[3].toInt(),
            freqMHz     = Float.fromBits(arr[4].toInt())
        )
    }
}
