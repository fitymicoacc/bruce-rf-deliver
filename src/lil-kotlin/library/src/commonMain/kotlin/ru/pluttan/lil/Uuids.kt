package ru.pluttan.lil

/**
 * Bluetooth GATT identifiers advertised by the Bruce-RF firmware. Must match
 * `bruce-rf/src/modules/ble_rf/ble_rf_service.h` and `liblil/include/lil.h`.
 */
object LilUuids {
    const val SERVICE     = "12345678-1234-5678-1234-56789abcdef0"
    const val CHAR_CMD    = "12345678-1234-5678-1234-56789abcdef1"
    const val CHAR_SIGNAL = "12345678-1234-5678-1234-56789abcdef2"
    const val CHAR_STATUS = "12345678-1234-5678-1234-56789abcdef3"

    /** Peripheral advertises this name — used as a filter fallback when
     *  service UUID filtering is not available on the platform. */
    const val ADVERTISED_NAME = "Bruce-RF"
}

/** Signal notification header bytes (first byte of a 0x80 or 0x81 frame). */
object LilHeaders {
    const val DECODED_SIGNAL = 0x80.toByte()
    const val RAW_TIMINGS    = 0x81.toByte()
}

/** Command IDs written to CHAR_CMD. */
enum class LilCmd(val byte: Byte) {
    START_LISTEN(0x01.toByte()),
    STOP(0x02.toByte()),
    PLAY(0x03.toByte()),
    PING(0x04.toByte()),
    SCAN_RANGES(0x05.toByte()),
    SCAN_LIST(0x06.toByte())
}
