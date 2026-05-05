package ru.pluttan.lil

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull

class KotlinDecoderTest {

    /** Build the raw pulse train that a correct transmitter would emit for
     *  `code` on a non-inverted protocol. Matches synth_raw() in the
     *  liblil test harness: prepended 1T HIGH + sync LOW + data pairs. */
    private fun synth(protoId: Int, code: Long, bits: Int): IntArray {
        val p = KotlinDecoder.protocolById(protoId) ?: error("no proto $protoId")
        val T = p.T
        val out = mutableListOf<Int>()
        out += T                 // prepended sync HIGH (bruce-rf pollRx invariant)
        out += T * p.syncL       // sync LOW
        for (b in bits - 1 downTo 0) {
            val bit = ((code ushr b) and 1L).toInt()
            if (bit == 1) {
                out += T * p.oneH
                out += T * p.oneL
            } else {
                out += T * p.zeroH
                out += T * p.zeroL
            }
        }
        return out.toIntArray()
    }

    @Test
    fun decodesPt2262_24bit_roundTrip() {
        val raw = synth(1, 0xA1B2C5L, 24)
        val s = KotlinDecoder.decodeDurations(raw, 433.92f)
        assertNotNull(s)
        assertEquals(1,          s.protocol)
        assertEquals(0xA1B2C5uL, s.key)
        assertEquals(24,         s.bits)
        assertEquals(350,        s.pulseLength)
        assertEquals(433.92f,    s.freqMHz)
    }

    @Test
    fun decodesEv1527_24bit_roundTrip() {
        val raw = synth(4, 0x55AA33L, 24)
        val s = KotlinDecoder.decodeDurations(raw, 433.92f)
        assertNotNull(s)
        assertEquals(4,          s.protocol)
        assertEquals(0x55AA33uL, s.key)
        assertEquals(24,         s.bits)
    }

    @Test
    fun decodesHx2262_keyAndBitsCorrect() {
        // HX2262 overlaps PT2262 in scoring — brute force settles on PT2262
        // first. We assert only on key and bits, not protocol id. Documented
        // as inherited behaviour from the reference scorer.
        val raw = synth(3, 0x010203L, 24)
        val s = KotlinDecoder.decodeDurations(raw, 433.92f)
        assertNotNull(s)
        assertEquals(0x010203uL, s.key)
        assertEquals(24,         s.bits)
    }

    @Test
    fun rejectsTooShortBuffer() {
        val s = KotlinDecoder.decodeDurations(intArrayOf(100, 100, 100, 100), 433.92f)
        assertNull(s)
    }

    @Test
    fun padsOffByOneBitCount() {
        // Drop the first data pair → decoder sees 23 bits of a 24-bit code,
        // pads the reported count back to the nearest standard bit count.
        val full = synth(1, 0xA1B2C5L, 24)
        val truncated = IntArray(full.size - 2) { i ->
            if (i < 2) full[i] else full[i + 2]
        }
        val s = KotlinDecoder.decodeDurations(truncated, 433.92f)
        assertNotNull(s)
        assertEquals(24, s.bits)
    }

    @Test
    fun unpackRawTimings_thenDecode_wiresTogether() {
        // Simulate a 0x81 notification from the device and hand the parsed
        // raw timings to KotlinDecoder — this is the path a JS client would
        // take because it has no native liblil.
        val raw = synth(1, 0xDEADBEEFL, 32)
        val freq = 433.92f
        val rawBits = freq.toRawBits()
        val payload = ByteArray(7 + raw.size * 2).apply {
            this[0] = LilHeaders.RAW_TIMINGS
            this[1] = (rawBits         and 0xFF).toByte()
            this[2] = ((rawBits ushr  8) and 0xFF).toByte()
            this[3] = ((rawBits ushr 16) and 0xFF).toByte()
            this[4] = ((rawBits ushr 24) and 0xFF).toByte()
            this[5] = raw.size.toByte()
            this[6] = 1  // start_level
            for (i in raw.indices) {
                val d = raw[i] and 0xFFFF
                this[7 + i * 2]     = (d and 0xFF).toByte()
                this[7 + i * 2 + 1] = ((d ushr 8) and 0xFF).toByte()
            }
        }
        val parsed = Protocol.unpackRawTimings(payload)
        assertNotNull(parsed)

        val decoded = KotlinDecoder.decode(parsed)
        assertNotNull(decoded)
        assertEquals(0xDEADBEEFuL, decoded.key)
        assertEquals(32,           decoded.bits)
    }
}
