package ru.pluttan.lil

/**
 * JVM actual: loads `liblil.{so,dylib,dll}` from either `java.library.path`
 * or the consuming app's resources. The JNI entry point
 * `Java_ru_pluttan_lil_LilNativeDecoder_nativeDecode` is provided by
 * `liblil/src/jni/lil_jni.c` when liblil is built with
 * `-DLIBLIL_BUILD_JNI=ON` (same flag as the Android build).
 *
 * If no native lib is linkable, [decode] falls back to [KotlinDecoder].
 */
actual object LilNativeDecoder {
    @Volatile private var linked = false

    init {
        runCatching {
            System.loadLibrary("lil")
            linked = true
        }
    }

    @JvmStatic
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
