package ru.pluttan.lil

/**
 * Pure-Kotlin implementation of the Lil wire protocol. On platforms that
 * ship with the `liblil` native library (Android, iOS, JVM, macOS) this
 * is kept in lock-step with the C implementation and used as a cross-check.
 * On JS this is the only implementation.
 *
 * All multi-byte fields are little-endian.
 */
object Protocol {

    // ---------- Pack ----------

    /** 5 bytes: cmd(1) + freq(f32 LE). */
    fun packStartListen(freqMHz: Float): ByteArray {
        val buf = ByteArray(5)
        buf[0] = LilCmd.START_LISTEN.byte
        writeFloatLE(buf, 1, freqMHz)
        return buf
    }

    /** 1 byte: cmd. */
    fun packStop(): ByteArray = byteArrayOf(LilCmd.STOP.byte)

    /** 1 byte: cmd. */
    fun packPing(): ByteArray = byteArrayOf(LilCmd.PING.byte)

    /** 17 bytes: cmd(1)+proto(1)+key(u64)+bits(1)+pulse(u16)+freq(f32). */
    fun packPlay(signal: LilSignal): ByteArray {
        val buf = ByteArray(17)
        var o = 0
        buf[o++] = LilCmd.PLAY.byte
        buf[o++] = signal.protocol.toByte()
        writeULongLE(buf, o, signal.key); o += 8
        buf[o++] = signal.bits.toByte()
        writeUShortLE(buf, o, signal.pulseLength.toUShort()); o += 2
        writeFloatLE(buf, o, signal.freqMHz)
        return buf
    }

    fun packScanList(freqsMHz: FloatArray, dwellMs: UShort): ByteArray {
        require(freqsMHz.size <= 32) { "SCAN_LIST supports at most 32 frequencies" }
        val buf = ByteArray(4 + freqsMHz.size * 4)
        var o = 0
        buf[o++] = LilCmd.SCAN_LIST.byte
        writeUShortLE(buf, o, dwellMs); o += 2
        buf[o++] = freqsMHz.size.toByte()
        for (f in freqsMHz) {
            writeFloatLE(buf, o, f); o += 4
        }
        return buf
    }

    fun packScanRanges(ranges: List<LilFreqRange>, dwellMs: UShort): ByteArray {
        require(ranges.size <= 8) { "SCAN_RANGES supports at most 8 ranges" }
        val buf = ByteArray(4 + ranges.size * 12)
        var o = 0
        buf[o++] = LilCmd.SCAN_RANGES.byte
        writeUShortLE(buf, o, dwellMs); o += 2
        buf[o++] = ranges.size.toByte()
        for (r in ranges) {
            writeFloatLE(buf, o, r.startMHz); o += 4
            writeFloatLE(buf, o, r.endMHz);   o += 4
            writeFloatLE(buf, o, r.stepMHz);  o += 4
        }
        return buf
    }

    // ---------- Unpack ----------

    /** Parse a 0x80 decoded-signal notification. Returns null on bad frame. */
    fun unpackSignal(data: ByteArray): LilSignal? {
        if (data.size < 17 || data[0] != LilHeaders.DECODED_SIGNAL) return null
        var o = 1
        val protocol = data[o].toInt() and 0xFF; o += 1
        val key = readULongLE(data, o); o += 8
        val bits = data[o].toInt() and 0xFF; o += 1
        val pulse = readUShortLE(data, o).toInt(); o += 2
        val freq = readFloatLE(data, o)
        return LilSignal(protocol, key, bits, pulse, freq)
    }

    /** Parse a 0x81 raw-timings notification. Returns null on bad frame. */
    fun unpackRawTimings(data: ByteArray): LilRawTimings? {
        if (data.size < 7 || data[0] != LilHeaders.RAW_TIMINGS) return null
        val freq = readFloatLE(data, 1)
        val declared = data[5].toInt() and 0xFF
        val startLevel = data[6].toInt() and 0xFF
        val payload = (data.size - 7) / 2
        val count = minOf(declared, payload)
        val durations = UShortArray(count)
        for (i in 0 until count) {
            durations[i] = readUShortLE(data, 7 + i * 2)
        }
        return LilRawTimings(freq, startLevel, durations)
    }

    /** Parse a 2-byte status payload. Returns null on bad frame. */
    fun unpackStatus(data: ByteArray): LilStatus? {
        if (data.size < 2) return null
        return LilStatus(
            LilDeviceState.fromByte(data[0]),
            LilDeviceError.fromByte(data[1])
        )
    }

    // ---------- LE helpers ----------

    private fun writeUShortLE(buf: ByteArray, off: Int, v: UShort) {
        val n = v.toInt()
        buf[off]     = (n and 0xFF).toByte()
        buf[off + 1] = ((n ushr 8) and 0xFF).toByte()
    }

    private fun writeULongLE(buf: ByteArray, off: Int, v: ULong) {
        var x = v.toLong()
        for (i in 0 until 8) {
            buf[off + i] = (x and 0xFF).toByte()
            x = x ushr 8
        }
    }

    private fun writeFloatLE(buf: ByteArray, off: Int, v: Float) {
        val raw = v.toRawBits()
        buf[off]     = (raw and 0xFF).toByte()
        buf[off + 1] = ((raw ushr 8) and 0xFF).toByte()
        buf[off + 2] = ((raw ushr 16) and 0xFF).toByte()
        buf[off + 3] = ((raw ushr 24) and 0xFF).toByte()
    }

    private fun readUShortLE(buf: ByteArray, off: Int): UShort {
        val lo = buf[off].toInt() and 0xFF
        val hi = buf[off + 1].toInt() and 0xFF
        return (lo or (hi shl 8)).toUShort()
    }

    private fun readULongLE(buf: ByteArray, off: Int): ULong {
        var v = 0L
        for (i in 0 until 8) {
            v = v or ((buf[off + i].toLong() and 0xFF) shl (i * 8))
        }
        return v.toULong()
    }

    private fun readFloatLE(buf: ByteArray, off: Int): Float {
        val b0 = buf[off].toInt() and 0xFF
        val b1 = buf[off + 1].toInt() and 0xFF
        val b2 = buf[off + 2].toInt() and 0xFF
        val b3 = buf[off + 3].toInt() and 0xFF
        val raw = b0 or (b1 shl 8) or (b2 shl 16) or (b3 shl 24)
        return Float.fromBits(raw)
    }
}
