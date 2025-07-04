# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

cmake_minimum_required(VERSION 3.16)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/lib/msh3.ver" msh3_ver)
if(NOT msh3_ver MATCHES ".*#define VER_MAJOR ([0-9]+).*#define VER_MINOR ([0-9]+).*#define VER_PATCH ([0-9]+).*")
    message(FATAL_ERROR "Cannot parse version from lib/msh3.ver")
endif()
set(msh3_ver "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")

message(STATUS "msh3: Configuration start...")
project(msh3 VERSION "${msh3_ver}")

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type on single-configuration generators")

option(MSH3_TOOL "Build tool" OFF)
option(MSH3_PING "Build ping tool" OFF)
option(MSH3_TEST "Build tests" OFF)

set(MSH3_VER_BUILD_ID "0" CACHE STRING "The version build ID")
message(STATUS "msh3: Version Build ID: ${MSH3_VER_BUILD_ID}")

set(MSH3_VER_SUFFIX "-private" CACHE STRING "The version suffix")
message(STATUS "msh3: Version Suffix: ${MSH3_VER_SUFFIX}")

# use, i.e. don't skip the full RPATH for the build tree
set(CMAKE_SKIP_BUILD_RPATH FALSE)

# when building, don't use the install RPATH already
# (but later on when installing)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

# add the automatically determined parts of the RPATH
# which point to directories outside the build tree to the install RPATH
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

if (WIN32)
    # Statically link the OS included part of the runtime.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /NODEFAULTLIB:libucrt.lib /DEFAULTLIB:ucrt.lib")

    add_compile_definitions(WIN32_LEAN_AND_MEAN)
    if(HAS_SPECTRE)
        add_compile_options(/Qspectre)
    endif()
    # Compile/link flags
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /GL /Zi")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /GL /Zi")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG /IGNORE:4075 /DEBUG /OPT:REF /OPT:ICF")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG /IGNORE:4075 /DEBUG /OPT:REF /OPT:ICF")
endif()

set(MSH3_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/bin CACHE STRING "Output directory for build artifacts")
message(STATUS "msh3: Output set to ${MSH3_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${MSH3_OUTPUT_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${MSH3_OUTPUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${MSH3_OUTPUT_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${MSH3_OUTPUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${MSH3_OUTPUT_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${MSH3_OUTPUT_DIR})

# ls-qpack dependency
option(MSH3_USE_EXTERNAL_LSQPACK "Use an external ls-qpack installation")
if(MSH3_USE_EXTERNAL_LSQPACK)
    find_package(ls-qpack CONFIG REQUIRED)
else()
    # Configure and build
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    option(LSQPACK_TESTS "Build tests" OFF)
    option(LSQPACK_BIN "Build binaries" OFF)
    add_subdirectory(ls-qpack)
endif()

# msquic dependency
option(MSH3_USE_EXTERNAL_MSQUIC "Use an external msquic installation")
if(MSH3_USE_EXTERNAL_MSQUIC)
    find_package(msquic CONFIG REQUIRED)
else()
    # Configure and build
    if (WIN32)
        set(QUIC_TLS_LIB "schannel" CACHE STRING "TLS Library to use")
    else()
        set(QUIC_TLS_LIB "quictls" CACHE STRING "TLS Library to use")
    endif()
    set(QUIC_BUILD_SHARED ON CACHE BOOL "Builds MsQuic as a dynamic library")
    set(QUIC_ENABLE_LOGGING ON CACHE BOOL "Enable MsQuic logging")
    set(QUIC_OUTPUT_DIR ${MSH3_OUTPUT_DIR} CACHE STRING "Output directory for build artifacts")
    add_subdirectory(msquic)
endif()

# Build msh3 library (and cmd line tool).
add_subdirectory(lib)
if (MSH3_TOOL)
    add_subdirectory(tool)
endif()
if (MSH3_PING)
    add_subdirectory(ping)
endif()
if (MSH3_TEST)
    add_subdirectory(test)
endif()
