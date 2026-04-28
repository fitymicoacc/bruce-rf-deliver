cmake_minimum_required(VERSION 3.16)

project(liblil
    VERSION 0.1.0
    DESCRIPTION "Lil wire protocol + RMT decoder for Bruce-RF"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(LIBLIL_BUILD_TESTS  "Build Unity-based unit tests"  ON)
option(LIBLIL_BUILD_SHARED "Build shared library"          ON)
option(LIBLIL_BUILD_STATIC "Build static library"          ON)
option(LIBLIL_BUILD_JNI    "Include JNI shim for Android / JVM callers" OFF)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

set(LIBLIL_SOURCES
    src/lil_protocols.c
    src/lil_wire.c
    src/lil_decoder.c
)

set(LIBLIL_HEADERS
    include/lil.h
)

if(LIBLIL_BUILD_STATIC)
    add_library(lil_static STATIC ${LIBLIL_SOURCES})
    target_include_directories(lil_static PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    set_target_properties(lil_static PROPERTIES
        OUTPUT_NAME lil
        PUBLIC_HEADER "${LIBLIL_HEADERS}"
    )
endif()

if(LIBLIL_BUILD_SHARED)
    add_library(lil_shared SHARED ${LIBLIL_SOURCES})
    target_include_directories(lil_shared PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    set_target_properties(lil_shared PROPERTIES
        OUTPUT_NAME lil
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        PUBLIC_HEADER "${LIBLIL_HEADERS}"
    )
endif()

# Preferred target for consumers — pick whichever is enabled
if(LIBLIL_BUILD_SHARED)
    add_library(lil::lil ALIAS lil_shared)
elseif(LIBLIL_BUILD_STATIC)
    add_library(lil::lil ALIAS lil_static)
endif()

include(GNUInstallDirs)
set(_lil_install_targets "")
if(LIBLIL_BUILD_STATIC)
    list(APPEND _lil_install_targets lil_static)
endif()
if(LIBLIL_BUILD_SHARED)
    list(APPEND _lil_install_targets lil_shared)
endif()
if(_lil_install_targets)
    install(
        TARGETS ${_lil_install_targets}
        EXPORT lilTargets
        LIBRARY       DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE       DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME       DESTINATION ${CMAKE_INSTALL_BINDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

if(LIBLIL_BUILD_JNI)
    # Android NDK provides jni.h through its toolchain; elsewhere we need
    # the JDK's headers.
    if(NOT ANDROID)
        find_package(JNI REQUIRED)
    endif()
    set(LIBLIL_JNI_SOURCES src/jni/lil_jni.c)
    if(TARGET lil_shared)
        target_sources(lil_shared PRIVATE ${LIBLIL_JNI_SOURCES})
        if(NOT ANDROID)
            target_include_directories(lil_shared PRIVATE ${JNI_INCLUDE_DIRS})
        endif()
    endif()
    if(TARGET lil_static)
        target_sources(lil_static PRIVATE ${LIBLIL_JNI_SOURCES})
        if(NOT ANDROID)
            target_include_directories(lil_static PRIVATE ${JNI_INCLUDE_DIRS})
        endif()
    endif()
endif()

if(LIBLIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
