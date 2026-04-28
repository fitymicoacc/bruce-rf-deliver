# MinGW-w64 cross-compile toolchain for Windows .dll / .lib from Linux or macOS.
#
# Usage:
#   cmake -S liblil -B build-win64 \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.toolchain.cmake
#   cmake --build build-win64
#
# Requires mingw-w64 installed:
#   Debian/Ubuntu:  sudo apt install mingw-w64
#   macOS (brew):   brew install mingw-w64
#   Arch:           sudo pacman -S mingw-w64-gcc

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_mingw_prefix x86_64-w64-mingw32)
find_program(CMAKE_C_COMPILER       NAMES ${_mingw_prefix}-gcc      REQUIRED)
find_program(CMAKE_CXX_COMPILER     NAMES ${_mingw_prefix}-g++      REQUIRED)
find_program(CMAKE_RC_COMPILER      NAMES ${_mingw_prefix}-windres)
find_program(CMAKE_AR               NAMES ${_mingw_prefix}-ar       REQUIRED)
find_program(CMAKE_RANLIB           NAMES ${_mingw_prefix}-ranlib   REQUIRED)
find_program(CMAKE_STRIP            NAMES ${_mingw_prefix}-strip)

# Where to look for target toolchain headers/libraries
set(CMAKE_FIND_ROOT_PATH /usr/${_mingw_prefix} /usr/local/${_mingw_prefix})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Static runtime — avoid hunting libgcc_s_seh-1.dll / libstdc++-6.dll at runtime
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-static-libgcc")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static-libgcc")
