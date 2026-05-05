package ru.pluttan.lil

/**
 * JS actual. No C interop on Web; the decoder delegates to [KotlinDecoder]
 * which ports `liblil/src/lil_decoder.c` 1:1 in pure Kotlin. Matches the
 * native implementation within the known scorer ambiguities (see
 * `KotlinDecoder` header for caveats).
 */
actual object LilNativeDecoder {
    actual fun decode(raw: LilRawTimings): LilSignal? = KotlinDecoder.decode(raw)
}
