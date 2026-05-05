package ru.pluttan.lil

import com.juul.kable.Peripheral
import com.juul.kable.characteristicOf
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch

/**
 * High-level client for a Bruce-RF BLE peripheral. Suspending methods write
 * commands; [signals] and [status] are hot flows fed by GATT notifications.
 *
 * Platform BLE scanning and peripheral construction live in the sample app
 * or caller code — this class takes an already-resolved Kable [Peripheral]
 * so it stays testable without the system BLE stack.
 *
 * Typical usage:
 * ```
 * val device = LilDevice(peripheral, scope)
 * device.connect()
 * scope.launch { device.signals.collect { println("got $it") } }
 * device.startListen(433.92f)
 * device.play(signal)
 * ```
 */
class LilDevice(
    private val peripheral: Peripheral,
    private val scope: CoroutineScope
) {
    @Suppress("DEPRECATION_ERROR")
    private val cmdChar    = characteristicOf(LilUuids.SERVICE, LilUuids.CHAR_CMD)
    @Suppress("DEPRECATION_ERROR")
    private val signalChar = characteristicOf(LilUuids.SERVICE, LilUuids.CHAR_SIGNAL)
    @Suppress("DEPRECATION_ERROR")
    private val statusChar = characteristicOf(LilUuids.SERVICE, LilUuids.CHAR_STATUS)

    private val _status = MutableStateFlow(LilStatus(LilDeviceState.IDLE, LilDeviceError.NONE))
    val status: StateFlow<LilStatus> = _status.asStateFlow()

    sealed interface Notification {
        data class Decoded(val signal: LilSignal) : Notification
        data class Raw(val raw: LilRawTimings)    : Notification
    }

    /**
     * Hot flow of notifications from CHAR_SIGNAL. Either a fully decoded
     * signal (first byte 0x80) or a raw-timing frame (first byte 0x81) that
     * the client may feed through the native `liblil` decoder (or the pure
     * Kotlin [Protocol] helpers).
     */
    val signals: Flow<Notification> = peripheral.observe(signalChar).map { bytes ->
        when {
            bytes.isEmpty() -> error("Empty CHAR_SIGNAL notification")
            bytes[0] == LilHeaders.DECODED_SIGNAL -> {
                Notification.Decoded(
                    Protocol.unpackSignal(bytes)
                        ?: error("Malformed 0x80 frame, size=${bytes.size}")
                )
            }
            bytes[0] == LilHeaders.RAW_TIMINGS -> {
                Notification.Raw(
                    Protocol.unpackRawTimings(bytes)
                        ?: error("Malformed 0x81 frame, size=${bytes.size}")
                )
            }
            else -> {
                val hex = (bytes[0].toInt() and 0xFF).toString(16).uppercase().padStart(2, '0')
                error("Unknown CHAR_SIGNAL header 0x$hex")
            }
        }
    }

    /** Connect to the peripheral and start forwarding CHAR_STATUS notifications. */
    suspend fun connect() {
        peripheral.connect()
        scope.launch {
            peripheral.observe(statusChar).collect { bytes ->
                Protocol.unpackStatus(bytes)?.let { _status.value = it }
            }
        }
    }

    suspend fun disconnect() {
        peripheral.disconnect()
    }

    suspend fun startListen(freqMHz: Float) =
        peripheral.write(cmdChar, Protocol.packStartListen(freqMHz))

    suspend fun stopListen() =
        peripheral.write(cmdChar, Protocol.packStop())

    suspend fun play(signal: LilSignal) =
        peripheral.write(cmdChar, Protocol.packPlay(signal))

    suspend fun ping() =
        peripheral.write(cmdChar, Protocol.packPing())

    suspend fun scanList(freqsMHz: FloatArray, dwellMs: UShort = 200u) =
        peripheral.write(cmdChar, Protocol.packScanList(freqsMHz, dwellMs))

    suspend fun scanRanges(ranges: List<LilFreqRange>, dwellMs: UShort = 200u) =
        peripheral.write(cmdChar, Protocol.packScanRanges(ranges, dwellMs))
}

/**
 * Native-decoder bridge. On Android / iOS / JVM / macOS this forwards to
 * the `liblil` shared object via cinterop or JNI; on JS it falls back to
 * a pure-Kotlin brute-force decoder (still TODO — structural stub).
 *
 * Actual implementations live in platform source sets.
 */
expect object LilNativeDecoder {
    fun decode(raw: LilRawTimings): LilSignal?
}
