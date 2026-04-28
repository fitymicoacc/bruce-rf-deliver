# Thin wrapper over the official Android NDK toolchain.
#
# Usage:
#   cmake -S liblil -B build-android-arm64 \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/android-ndk.toolchain.cmake \
#         -DANDROID_ABI=arm64-v8a \
#         -DANDROID_PLATFORM=android-24
#
# Required env var: ANDROID_NDK_HOME (path to ndk root, e.g. ~/Library/Android/sdk/ndk/26.3.11579264)
# Optional:         ANDROID_ABI (default arm64-v8a), ANDROID_PLATFORM (default android-24)

if(NOT DEFINED ENV{ANDROID_NDK_HOME} AND NOT DEFINED ANDROID_NDK_HOME)
    message(FATAL_ERROR "ANDROID_NDK_HOME is not set. Install Android NDK r23+ and export the path.")
endif()

if(DEFINED ENV{ANDROID_NDK_HOME})
    set(ANDROID_NDK_HOME "$ENV{ANDROID_NDK_HOME}")
endif()

set(_ndk_toolchain "${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake")
if(NOT EXISTS "${_ndk_toolchain}")
    message(FATAL_ERROR "NDK toolchain not found at ${_ndk_toolchain} — check ANDROID_NDK_HOME.")
endif()

if(NOT DEFINED ANDROID_ABI)
    set(ANDROID_ABI "arm64-v8a" CACHE STRING "Android ABI (arm64-v8a, armeabi-v7a, x86_64, x86)")
endif()
if(NOT DEFINED ANDROID_PLATFORM)
    set(ANDROID_PLATFORM "android-24" CACHE STRING "Android API level (min 21 for NDK r23+)")
endif()
if(NOT DEFINED ANDROID_STL)
    set(ANDROID_STL "c++_static" CACHE STRING "Android STL variant")
endif()

include("${_ndk_toolchain}")
