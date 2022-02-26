# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

project(msh3)
cmake_minimum_required(VERSION 3.16)

option(MSH3_TOOL "Build tool" OFF)

# Public include header.
add_library(msh3_headers INTERFACE)
target_include_directories(msh3_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Configure and build ls-qpack dependency.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
option(LSQPACK_TESTS "Build tests" OFF)
option(LSQPACK_BIN "Build binaries" OFF)
add_subdirectory(ls-qpack)

# Configure and build msquic dependency.
set(QUIC_TLS "openssl")
set(QUIC_ENABLE_LOGGING ON CACHE BOOL "Enable MsQuic logging")
set(CMAKE_BUILD_TYPE "Release")
add_subdirectory(msquic)
target_compile_features(inc INTERFACE cxx_std_20)

# Build msh3 library (and cmd line tool).
add_subdirectory(lib)
if (MSH3_TOOL)
    add_subdirectory(tool)
endif()
