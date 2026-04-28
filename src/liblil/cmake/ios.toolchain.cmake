# Minimal iOS toolchain. For production builds the scripts/build-ios.sh
# wrapper drives two separate cmake builds (device + simulator) and then
# stitches them with `xcodebuild -create-xcframework`.
#
# This file only carries the common flags shared by both builds.
#
# Expected invocation (from scripts/build-ios.sh):
#   cmake -S liblil -B build-ios-device \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/ios.toolchain.cmake \
#         -DLIL_IOS_SDK=iphoneos       \
#         -DLIL_IOS_ARCH=arm64         \
#         -DLIL_IOS_DEPLOYMENT_TARGET=13.0 \
#         -G Xcode
#   cmake --build build-ios-device --config Release
#
# For the simulator: -DLIL_IOS_SDK=iphonesimulator -DLIL_IOS_ARCH="arm64;x86_64"
#
# This toolchain assumes Xcode + command-line tools are installed (mac-only).

set(CMAKE_SYSTEM_NAME       iOS)
set(CMAKE_SYSTEM_VERSION    "${LIL_IOS_DEPLOYMENT_TARGET}")

if(NOT DEFINED LIL_IOS_SDK)
    set(LIL_IOS_SDK "iphoneos" CACHE STRING "iphoneos | iphonesimulator")
endif()
if(NOT DEFINED LIL_IOS_DEPLOYMENT_TARGET)
    set(LIL_IOS_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum iOS version")
endif()
if(NOT DEFINED LIL_IOS_ARCH)
    if(LIL_IOS_SDK STREQUAL "iphoneos")
        set(LIL_IOS_ARCH "arm64" CACHE STRING "Device arch")
    else()
        set(LIL_IOS_ARCH "arm64;x86_64" CACHE STRING "Simulator archs")
    endif()
endif()

set(CMAKE_OSX_SYSROOT                "${LIL_IOS_SDK}")
set(CMAKE_OSX_ARCHITECTURES          "${LIL_IOS_ARCH}")
set(CMAKE_OSX_DEPLOYMENT_TARGET      "${LIL_IOS_DEPLOYMENT_TARGET}")

# Device vs simulator: honour the SDK so xcodebuild can stitch the xcframework
set(CMAKE_XCODE_ATTRIBUTE_SDKROOT                       "${LIL_IOS_SDK}")
set(CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET    "${LIL_IOS_DEPLOYMENT_TARGET}")
set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH              NO)
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY            "")
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED         NO)
set(CMAKE_XCODE_ATTRIBUTE_SKIP_INSTALL                  NO)

# PIC is mandatory on iOS
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
