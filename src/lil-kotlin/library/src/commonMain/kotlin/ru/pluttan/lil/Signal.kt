package ru.pluttan.lil

/**
 * A decoded sub-GHz signal. Matches the 0x80 notification payload and the
 * body of the PLAY command, byte-for-byte.
 *
 * @property protocol  RCSwitch protocol id (1..23). 0 = unknown / RAW.
 * @property key       Signal code. Up to 64 bits of data; MSB is the most
 *                     significant bit of the transmitted frame.
 * @property bits      Number of data bits in [key].
 * @property pulseLength Base T in microseconds.
 * @property freqMHz   Carrier frequency in MHz.
 */
data class LilSignal(
    val protocol: Int,
    val key: ULong,
    val bits: Int,
    val pulseLength: Int,
    val freqMHz: Float
)

/** Raw RMT timings as captured by the device (header 0x81). */
data class LilRawTimings(
    val freqMHz: Float,
    val startLevel: Int,
    val durations: UShortArray
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is LilRawTimings) return false
        return freqMHz == other.freqMHz &&
               startLevel == other.startLevel &&
               durations.contentEquals(other.durations)
    }
    override fun hashCode(): Int {
        var r = freqMHz.hashCode()
        r = 31 * r + startLevel
        r = 31 * r + durations.contentHashCode()
        return r
    }
}

/** Frequency range used by SCAN_RANGES. */
data class LilFreqRange(val startMHz: Float, val endMHz: Float, val stepMHz: Float)

/** Device status (CHAR_STATUS notification payload, 2 bytes). */
data class LilStatus(val state: LilDeviceState, val error: LilDeviceError)

enum class LilDeviceState(val byte: Byte) {
    IDLE(0x00.toByte()),
    LISTENING(0x01.toByte()),
    TRANSMITTING(0x02.toByte()),
    SCANNING(0x03.toByte()),
    ERROR(0xFF.toByte());

    companion object {
        fun fromByte(b: Byte): LilDeviceState =
            entries.firstOrNull { it.byte == b } ?: ERROR
    }
}

enum class LilDeviceError(val byte: Byte) {
    NONE(0x00.toByte()),
    CC1101_FAIL(0x01.toByte()),
    INVALID_CMD(0x02.toByte()),
    TX_FAIL(0x03.toByte());

    companion object {
        fun fromByte(b: Byte): LilDeviceError =
            entries.firstOrNull { it.byte == b } ?: NONE
    }
}
