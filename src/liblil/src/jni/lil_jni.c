/*
 * JNI shim exposing the liblil decoder to Kotlin/Java callers.
 *
 * Signature matches ru.pluttan.lil.LilNativeDecoder.nativeDecode from the
 * lil-kotlin module:
 *
 *     private external fun nativeDecode(
 *         durations: ShortArray,
 *         startLevel: Int,
 *         freqMHz: Float
 *     ): LongArray?
 *
 * Return layout, 5 longs:
 *   [0] protocol       (0..255, 0 = unknown/RAW)
 *   [1] key            (full 64 bits; Kotlin will reinterpret as ULong)
 *   [2] bits           (0..64)
 *   [3] pulse_length   (microseconds)
 *   [4] freq bits      (float raw bits, caller calls Float.fromBits)
 *
 * null on LIL_ERR_NO_MATCH or any decoder error — mirrors the Kotlin contract.
 *
 * Compiled only when -DLIBLIL_BUILD_JNI=ON. On Android the NDK provides
 * jni.h transparently; on JVM we pull it from find_package(JNI).
 */

#include <jni.h>
#include <string.h>

#include "lil.h"

JNIEXPORT jlongArray JNICALL
Java_ru_pluttan_lil_LilNativeDecoder_nativeDecode(
    JNIEnv* env,
    jobject thiz,
    jshortArray durations_ja,
    jint startLevel,
    jfloat freqMHz)
{
    (void)thiz;

    jsize n = (*env)->GetArrayLength(env, durations_ja);
    if (n <= 0) return NULL;
    if ((size_t)n > LIL_MAX_RAW_TIMINGS) n = (jsize)LIL_MAX_RAW_TIMINGS;

    jshort* src = (*env)->GetShortArrayElements(env, durations_ja, NULL);
    if (src == NULL) return NULL;

    uint16_t durations[LIL_MAX_RAW_TIMINGS];
    for (jsize i = 0; i < n; i++) {
        /* Kotlin UShort is passed in as signed jshort; re-widen through
         * uint16_t to avoid sign extension on negative values. */
        durations[i] = (uint16_t)((uint16_t)src[i]);
    }
    (*env)->ReleaseShortArrayElements(env, durations_ja, src, JNI_ABORT);

    lil_signal_t sig;
    memset(&sig, 0, sizeof(sig));
    lil_status_t status = lil_decode_raw_timings(
        durations, (size_t)n, (uint8_t)startLevel, (float)freqMHz, &sig);
    if (status != LIL_OK) return NULL;

    jlongArray out = (*env)->NewLongArray(env, 5);
    if (out == NULL) return NULL;

    uint32_t freq_bits;
    memcpy(&freq_bits, &sig.freq, sizeof(freq_bits));

    jlong buf[5];
    buf[0] = (jlong)sig.protocol;
    buf[1] = (jlong)sig.key;
    buf[2] = (jlong)sig.bits;
    buf[3] = (jlong)sig.pulse_length;
    buf[4] = (jlong)freq_bits;
    (*env)->SetLongArrayRegion(env, out, 0, 5, buf);
    return out;
}
