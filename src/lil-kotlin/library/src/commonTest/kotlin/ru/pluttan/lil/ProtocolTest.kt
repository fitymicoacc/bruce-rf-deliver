package ru.pluttan.lil

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull

class ProtocolTest {

    @Test
    fun packStartListen_layoutMatchesWire() {
        val bytes = Protocol.packStartListen(433.92f)
        assertEquals(5, bytes.size)
        assertEquals(0x01.toByte(), bytes[0])
        // Float32 LE round-trip
        val decodedFreq = Float.fromBits(
            (bytes[1].toInt() and 0xFF) or
            ((bytes[2].toInt() and 0xFF) shl 8) or
            ((bytes[3].toInt() and 0xFF) shl 16) or
            ((bytes[4].toInt() and 0xFF) shl 24)
        )
        assertEquals(433.92f, decodedFreq)
    }

    @Test
    fun packStop_singleByte() {
        val bytes = Protocol.packStop()
        assertEquals(1, bytes.size)
        assertEquals(0x02.toByte(), bytes[0])
    }

    @Test
    fun packPing_singleByte() {
        val bytes = Protocol.packPing()
        assertEquals(1, bytes.size)
        assertEquals(0x04.toByte(), bytes[0])
    }

    @Test
    fun packPlay_layoutMatchesWire() {
        val sig = LilSignal(
            protocol = 1, key = 0xA1B2C5uL, bits = 24,
            pulseLength = 349, freqMHz = 433.92f
        )
        val bytes = Protocol.packPlay(sig)
        assertEquals(17, bytes.size)
        assertEquals(0x03.toByte(), bytes[0])
        assertEquals(1.toByte(),    bytes[1])
    }

    @Test
    fun packPlayThenUnpackSignal_roundTrip() {
        val sig = LilSignal(1, 0xA1B2C5uL, 24, 349, 433.92f)
        val packed = Protocol.packPlay(sig)
        // Convert PLAY body (with cmd=0x03 header) to a 0x80 notification by
        // swapping the leading byte — payload layout is identical.
        val notify = packed.copyOf()
        notify[0] = LilHeaders.DECODED_SIGNAL
        val decoded = Protocol.unpackSignal(notify)
        assertNotNull(decoded)
        assertEquals(sig, decoded)
    }

    @Test
    fun unpackSignal_rejectsBadHeader() {
        val bad = ByteArray(17) { 0 }
        bad[0] = 0x42
        assertNull(Protocol.unpackSignal(bad))
    }

    @Test
    fun unpackSignal_rejectsShortFrame() {
        val tiny = ByteArray(10) { 0 }
        tiny[0] = LilHeaders.DECODED_SIGNAL
        assertNull(Protocol.unpackSignal(tiny))
    }

    @Test
    fun packScanList_layout() {
        val freqs = floatArrayOf(315.0f, 433.92f, 868.0f)
        val bytes = Protocol.packScanList(freqs, 200u)
        assertEquals(4 + 3 * 4, bytes.size)
        assertEquals(0x06.toByte(), bytes[0])
        val dwell = (bytes[1].toInt() and 0xFF) or ((bytes[2].toInt() and 0xFF) shl 8)
        assertEquals(200, dwell)
        assertEquals(3.toByte(), bytes[3])
    }

    @Test
    fun packScanRanges_layout() {
        val ranges = listOf(
            LilFreqRange(300f, 350f, 1f),
            LilFreqRange(430f, 440f, 0.5f)
        )
        val bytes = Protocol.packScanRanges(ranges, 250u)
        assertEquals(4 + 2 * 12, bytes.size)
        assertEquals(0x05.toByte(), bytes[0])
        assertEquals(2.toByte(),    bytes[3])
    }

    @Test
    fun unpackRawTimings_happyPath() {
        val freq = 433.92f
        val rawBits = freq.toRawBits()
        val durations = shortArrayOf(350, 1050, 1050, 350)
        val bytes = ByteArray(7 + durations.size * 2).apply {
            this[0] = LilHeaders.RAW_TIMINGS
            this[1] = (rawBits         and 0xFF).toByte()
            this[2] = ((rawBits ushr  8) and 0xFF).toByte()
            this[3] = ((rawBits ushr 16) and 0xFF).toByte()
            this[4] = ((rawBits ushr 24) and 0xFF).toByte()
            this[5] = durations.size.toByte()
            this[6] = 1  // start_level
            for (i in durations.indices) {
                val d = durations[i].toInt() and 0xFFFF
                this[7 + i * 2]     = (d and 0xFF).toByte()
                this[7 + i * 2 + 1] = ((d ushr 8) and 0xFF).toByte()
            }
        }
        val parsed = Protocol.unpackRawTimings(bytes)
        assertNotNull(parsed)
        assertEquals(freq, parsed.freqMHz)
        assertEquals(1, parsed.startLevel)
        assertEquals(4, parsed.durations.size)
        for (i in durations.indices) {
            assertEquals(durations[i].toInt() and 0xFFFF, parsed.durations[i].toInt())
        }
    }

    @Test
    fun unpackStatus_layout() {
        val parsed = Protocol.unpackStatus(byteArrayOf(0x01, 0x00))
        assertNotNull(parsed)
        assertEquals(LilDeviceState.LISTENING, parsed.state)
        assertEquals(LilDeviceError.NONE,      parsed.error)
    }

    @Test
    fun unpackStatus_unknownErrorFallsBackToNone() {
        val parsed = Protocol.unpackStatus(byteArrayOf(0x01, 0x7F))
        assertNotNull(parsed)
        // Unknown error byte maps to NONE by the enum helper
        assertEquals(LilDeviceError.NONE, parsed.error)
    }

    @Test
    fun deviceState_errorByteRoundTrip() {
        assertEquals(LilDeviceState.ERROR,
                     LilDeviceState.fromByte(0xFF.toByte()))
        assertEquals(LilDeviceState.SCANNING,
                     LilDeviceState.fromByte(0x03))
    }

    @Test
    fun packScanList_rejectsMoreThan32() {
        val freqs = FloatArray(33) { 433f }
        kotlin.runCatching { Protocol.packScanList(freqs, 100u) }
            .also { assertNotNull(it.exceptionOrNull()) }
    }

    @Test
    fun packScanRanges_rejectsMoreThan8() {
        val ranges = List(9) { LilFreqRange(300f, 400f, 1f) }
        kotlin.runCatching { Protocol.packScanRanges(ranges, 100u) }
            .also { assertNotNull(it.exceptionOrNull()) }
    }
}
