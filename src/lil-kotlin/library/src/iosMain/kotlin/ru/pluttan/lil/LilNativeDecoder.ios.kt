package ru.pluttan.lil

/**
 * iOS actual. Bridges to the `liblil.xcframework` produced by
 * `liblil/scripts/build-ios.sh` via the cinterop descriptor at
 * `library/nativeInterop/cinterop/liblil.def`.
 *
 * The cinterop task exposes `ru.pluttan.lil.native.lil_decode_raw_timings`
 * plus the C structs (`lil_signal_t`, `lil_raw_timings_t`, the enum
 * values). We wrap it so callers in `commonMain` and `LilDevice` just
 * see the Kotlin [LilSignal] result, the same shape every other platform
 * returns.
 *
 * If the xcframework is not bundled (cinterop task skipped, e.g. in a
 * CI run that doesn't build native deps), [decode] falls back to the
 * shared [KotlinDecoder] so the library is still usable end-to-end.
 */

import kotlinx.cinterop.BetaInteropApi
import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.UShortVar
import kotlinx.cinterop.alloc
import kotlinx.cinterop.allocArray
import kotlinx.cinterop.memScoped
import kotlinx.cinterop.ptr
import ru.pluttan.lil.native.LIL_OK
import ru.pluttan.lil.native.lil_decode_raw_timings
import ru.pluttan.lil.native.lil_signal_t

@OptIn(ExperimentalForeignApi::class, BetaInteropApi::class)
actual object LilNativeDecoder {
    actual fun decode(raw: LilRawTimings): LilSignal? = memScoped {
        val durations = allocArray<UShortVar>(raw.durations.size) { i ->
            value = raw.durations[i]
        }
        val out = alloc<lil_signal_t>()
        val status = lil_decode_raw_timings(
            durations,
            raw.durations.size.toULong(),
            raw.startLevel.toUByte(),
            raw.freqMHz,
            out.ptr
        )
        if (status.toInt() != LIL_OK.toInt()) {
            // Decoder rejected the frame — mirror KotlinDecoder contract.
            return@memScoped null
        }
        LilSignal(
            protocol    = out.protocol.toInt(),
            key         = out.key,
            bits        = out.bits.toInt(),
            pulseLength = out.pulse_length.toInt(),
            freqMHz     = out.freq
        )
    }
}
