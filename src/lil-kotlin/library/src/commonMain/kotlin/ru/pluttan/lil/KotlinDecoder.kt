package ru.pluttan.lil

import kotlin.math.abs

/**
 * Pure-Kotlin port of desktop/src/renderer/src/lib/decoder.ts, same
 * algorithm as `liblil/src/lil_decoder.c`.
 *
 * Works on every KMP target without cinterop or JNI, so it is the default
 * decoder on Web (js) and a correctness cross-check against the native
 * [LilNativeDecoder] on Android/iOS/JVM.
 *
 * ## Algorithm
 *
 * 1. **Deglitch** — pulses shorter than [GLITCH_THRESHOLD] microseconds are
 *    merged with their neighbours (SPI/BLE ISR noise).
 * 2. **Sync candidates** — pairs whose larger half exceeds `median(samples) * 4`;
 *    falls back to the single longest pair when nothing passes the threshold.
 * 3. **Brute force** — every (sync, protocol, orientation) triple is tested.
 *    Sync halves must match within [SYNC_TOL_PCT] + 1T. Bits are read with
 *    AGC-weighted error (HIGH weight 3) and a [BIT_THRESH_PCT] threshold.
 *    A post-sync run is attempted first; if fewer than 12 bits land the
 *    pre-sync segment is re-tried.
 * 4. **Score** — standard bit counts (8/12/16/20/24/28/32/40/48/64) score
 *    150 per bit, non-standard 50; after-sync adds 50. Best score wins.
 *
 * Known caveats (inherited from the TypeScript reference — neither the C
 * port nor this one "fix" them; see `liblil/src/lil_decoder.c`):
 *
 * - Short-sync protocols (CAME 12, Protocol 23) have data pulses longer
 *   than half the sync pulse, so the decoder stops early and reports
 *   no match for genuine CAME frames.
 * - Protocols with overlapping sync signatures (PT2262 ↔ FAAC ↔ NICE ↔
 *   Blyss) collapse onto PT2262 because the scorer has no total-deviation
 *   tiebreak.
 */
object KotlinDecoder {
    private const val GLITCH_THRESHOLD = 30
    private const val SYNC_TOL_PCT     = 40
    private const val BIT_THRESH_PCT   = 60
    private const val HIGH_W           = 3
    private const val MIN_T            = 50
    private const val MAX_T            = 2000

    private val STD_BITS = intArrayOf(8, 12, 16, 20, 24, 28, 32, 40, 48, 64)

    /** RCSwitch protocol definition — mirrors `lil_rc_protocol_t` in lil.h. */
    internal data class RcProto(
        val id: Int,
        val name: String,
        val T: Int,
        val syncH: Int, val syncL: Int,
        val zeroH: Int, val zeroL: Int,
        val oneH: Int,  val oneL: Int,
        val inverted: Boolean
    )

    /** 23-entry protocol table, byte-for-byte aligned with `liblil/src/lil_protocols.c`. */
    internal val PROTOCOLS: List<RcProto> = listOf(
        RcProto( 1, "PT2262",        350,   1,  31,  1,  3,  3,  1, false),
        RcProto( 2, "SC5262",        650,   1,  10,  1,  2,  2,  1, false),
        RcProto( 3, "HX2262",        100,  30,  71,  4, 11,  9,  6, false),
        RcProto( 4, "EV1527",        380,   1,   6,  1,  3,  3,  1, false),
        RcProto( 5, "HT6P20B",       500,   6,  14,  1,  2,  2,  1, false),
        RcProto( 6, "HT6P20B~",      450,  23,   1,  1,  2,  2,  1, true),
        RcProto( 7, "HS2303-PT",     150,   2,  62,  1,  6,  6,  1, false),
        RcProto( 8, "Conrad RX",     200,   3, 130,  7, 16,  3, 16, false),
        RcProto( 9, "Conrad TX",     200, 130,   7, 16,  7, 16,  3, true),
        RcProto(10, "1ByOne",        365,  18,   1,  3,  1,  1,  3, true),
        RcProto(11, "HT12E",         270,  36,   1,  1,  2,  2,  1, true),
        RcProto(12, "SM5212",        320,  36,   1,  1,  2,  2,  1, true),
        RcProto(13, "Mumbi",         100,   3, 100,  3,  8,  8,  3, false),
        RcProto(14, "Blyss",         500,   1,  14,  1,  3,  3,  1, false),
        RcProto(15, "sc2260R4",      415,   1,  30,  1,  3,  4,  1, false),
        RcProto(16, "HomeNetWerks",  250,  20,  10,  1,  1,  3,  1, false),
        RcProto(17, "ORNO",           80,   3,  25,  3, 13, 11,  5, false),
        RcProto(18, "CLARUS",         82,   2,  65,  3,  5,  7,  1, false),
        RcProto(19, "NEC",           560,  16,   8,  1,  1,  1,  3, false),
        RcProto(20, "CAME 12",       250,   1,   3,  2,  1,  1,  2, false),
        RcProto(21, "FAAC",          330,   1,  34,  2,  1,  1,  2, false),
        RcProto(22, "NICE",          700,   1,  36,  2,  1,  1,  2, false),
        RcProto(23, "Protocol 23",   400,   0,  10,  2,  1,  1,  2, false)
    )

    fun decode(raw: LilRawTimings): LilSignal? {
        val ints = IntArray(raw.durations.size) { raw.durations[it].toInt() and 0xFFFF }
        return decodeDurations(ints, raw.freqMHz)
    }

    /**
     * Raw-integer entry point. Callers that already have a plain `IntArray`
     * (tests, JNI adapters) avoid the UShortArray allocation.
     */
    fun decodeDurations(durations: IntArray, freqMHz: Float): LilSignal? {
        if (durations.size < 6) return null
        val clean = deglitch(durations)
        if (clean.size < 6) return null
        val pairCount = clean.size / 2
        if (pairCount < 3) return null

        val sorted = clean.copyOf().also { it.sort() }
        val syncThreshold = sorted[clean.size / 2] * 4

        val syncCands = mutableListOf<Int>()
        for (i in 0 until pairCount) {
            val m = maxOf(clean[i * 2], clean[i * 2 + 1])
            if (m > syncThreshold) syncCands.add(i)
        }
        if (syncCands.isEmpty()) {
            var maxDur = 0
            var maxIdx = 0
            for (i in 0 until pairCount) {
                val m = maxOf(clean[i * 2], clean[i * 2 + 1])
                if (m > maxDur) { maxDur = m; maxIdx = i }
            }
            syncCands.add(maxIdx)
        }

        var bestScore = 0
        var best: LilSignal? = null
        for (syncIdx in syncCands) {
            for (proto in PROTOCOLS) {
                for (dur0IsH in BOOLEANS) {
                    val match = tryCombination(proto, clean, pairCount, syncIdx, dur0IsH)
                    if (match != null) {
                        val score = scoreResult(match.bits, match.afterSync)
                        if (score > bestScore) {
                            bestScore = score
                            best = LilSignal(
                                protocol = proto.id,
                                key = match.key.toULong(),
                                bits = match.bits,
                                pulseLength = match.T,
                                freqMHz = freqMHz
                            )
                        }
                    }
                }
            }
        }
        return best
    }

    private val BOOLEANS = booleanArrayOf(true, false).toList()

    private data class Match(val key: Long, val bits: Int, val T: Int, val afterSync: Boolean)

    private fun tryCombination(
        p: RcProto, clean: IntArray, pairCount: Int,
        syncIdx: Int, dur0IsH: Boolean
    ): Match? {
        val a = clean[syncIdx * 2]
        val b = clean[syncIdx * 2 + 1]
        val syncH = if (dur0IsH) a else b
        val syncL = if (dur0IsH) b else a

        val syncSum  = p.syncH + p.syncL
        if (syncSum == 0) return null
        val syncLong = maxOf(p.syncH, p.syncL)
        if (syncLong == 0) return null

        val syncLongDur = maxOf(syncH, syncL)
        val T = syncLongDur / syncLong
        if (T < MIN_T || T > MAX_T) return null

        val expH = T * p.syncH
        val expL = T * p.syncL
        val tolH = expH * SYNC_TOL_PCT / 100 + T
        val tolL = expL * SYNC_TOL_PCT / 100 + T
        if (abs(syncH - expH) > tolH) return null
        if (abs(syncL - expL) > tolL) return null

        var key = 0L
        var bits = 0
        var afterSync = true
        if (syncIdx + 1 < pairCount) {
            readBits(clean, syncIdx + 1, pairCount, dur0IsH, T, syncLongDur, p).also {
                key = it.first; bits = it.second
            }
        }

        if (bits < 12 && syncIdx >= 4) {
            val (k2, b2) = readBits(clean, 0, syncIdx, dur0IsH, T, syncLongDur, p)
            if (b2 > bits) {
                key = k2
                bits = b2
                afterSync = false
            }
        }

        if (bits < 4) return null
        if (key == 0L) return null

        // Off-by-one pad: RMT sometimes misses the first data pair
        for (std in STD_BITS) {
            if (bits == std - 1) { bits = std; break }
        }

        return Match(key, bits, T, afterSync)
    }

    private fun readBits(
        clean: IntArray, start: Int, end: Int,
        dur0IsH: Boolean, T: Int, syncLongDur: Int, p: RcProto
    ): Pair<Long, Int> {
        var key = 0L
        var bits = 0
        val maxZh = maxOf(p.zeroH, p.oneH)
        val maxZl = maxOf(p.zeroL, p.oneL)
        val bitLen = HIGH_W * T * maxZh + T * maxZl
        val errThresh = bitLen * BIT_THRESH_PCT / 100
        val syncStop = syncLongDur / 2

        var i = start
        while (i < end && bits < 64) {
            val a = clean[i * 2]
            val b = clean[i * 2 + 1]
            val dH = if (dur0IsH) a else b
            val dL = if (dur0IsH) b else a
            if (dH == 0 || dL == 0) break
            if (maxOf(dH, dL) > syncStop) break

            val errZero: Int
            val errOne: Int
            if (i == start && dH < T) {
                // AGC recovery — use LOW only
                errZero = abs(dL - T * p.zeroL)
                errOne  = abs(dL - T * p.oneL)
            } else {
                errZero = HIGH_W * abs(dH - T * p.zeroH) + abs(dL - T * p.zeroL)
                errOne  = HIGH_W * abs(dH - T * p.oneH)  + abs(dL - T * p.oneL)
            }

            val minErr = minOf(errZero, errOne)
            if (minErr > errThresh) break

            val bit = if (errOne < errZero) 1L else 0L
            key = (key shl 1) or bit
            bits++
            i++
        }
        return key to bits
    }

    private fun deglitch(input: IntArray): IntArray {
        val out = input.toMutableList()
        var i = 1
        while (i + 1 < out.size) {
            if (out[i] < GLITCH_THRESHOLD) {
                val merged = out[i - 1] + out[i] + out[i + 1]
                out[i - 1] = if (merged > 0xFFFF) 0xFFFF else merged
                out.removeAt(i + 1)   // remove i+1 first (indices shift otherwise)
                out.removeAt(i)
            } else {
                i++
            }
        }
        return out.toIntArray()
    }

    private fun scoreResult(bits: Int, afterSync: Boolean): Int {
        val std = STD_BITS.any { it == bits }
        val bonus = if (afterSync) 50 else 0
        return (if (std) bits * 150 else bits * 50) + bonus + bits
    }

    // Exposed for tests — synthesise a raw pulse train from a protocol def.
    internal fun protocolById(id: Int): RcProto? = PROTOCOLS.firstOrNull { it.id == id }
}
