# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

project(msh3)
cmake_minimum_required(VERSION 3.16)

set(QUIC_TLS "openssl")
set(QUIC_BUILD_TOOLS OFF CACHE BOOL "Don't use the tools")
set(QUIC_BUILD_TEST OFF CACHE BOOL "Don't need the tests")
set(QUIC_BUILD_PERF OFF CACHE BOOL "Don't need perf")
set(QUIC_ENABLE_LOGGING ON CACHE BOOL "Enable MsQuic logging")
set(CMAKE_BUILD_TYPE "Debug")
add_subdirectory(msquic)
target_compile_features(inc INTERFACE cxx_std_20)

add_subdirectory(ls-qpack)

add_library(msh3_headers INTERFACE)
target_include_directories(msh3_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(lib)
add_subdirectory(tool)
