# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

project(msh3)
cmake_minimum_required(VERSION 3.16)

option(MSH3_SERVER_SUPPORT "Enable server support" OFF)
option(MSH3_TOOL "Build tool" OFF)
option(MSH3_TEST "Build tests" OFF)

set(MSH3_VER_BUILD_ID "0" CACHE STRING "The version build ID")
message(STATUS "Version Build ID: ${MSH3_VER_BUILD_ID}")

set(MSH3_VER_SUFFIX "-private" CACHE STRING "The version suffix")
message(STATUS "Version Suffix: ${MSH3_VER_SUFFIX}")

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
    set(QUIC_COMMON_DEFINES WIN32_LEAN_AND_MEAN SECURITY_WIN32)
    # TODO - Get these to work on Linux
    list(APPEND MSH3_COMMON_DEFINES VER_BUILD_ID=${MSH3_VER_BUILD_ID})
    list(APPEND MSH3_COMMON_DEFINES VER_SUFFIX=${MSH3_VER_SUFFIX})
else()
    set(QUIC_COMMON_DEFINES _GNU_SOURCE)
endif()

if (MSH3_SERVER_SUPPORT)
    message(STATUS "Configuring for server support")
    list(APPEND MSH3_COMMON_DEFINES MSH3_SERVER_SUPPORT=1)
endif()

# Public include header.
add_library(msh3_headers INTERFACE)
target_compile_definitions(msh3_headers INTERFACE ${MSH3_COMMON_DEFINES})
target_include_directories(msh3_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Configure and build ls-qpack dependency.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
option(LSQPACK_TESTS "Build tests" OFF)
option(LSQPACK_BIN "Build binaries" OFF)
add_subdirectory(ls-qpack)

# Configure and build msquic dependency.
if (WIN32)
    set(QUIC_TLS "schannel" CACHE STRING "TLS Library to use")
else()
    set(QUIC_TLS "openssl" CACHE STRING "TLS Library to use")
endif()
set(QUIC_BUILD_SHARED ON CACHE BOOL "Builds MsQuic as a dynamic library")
set(QUIC_ENABLE_LOGGING ON CACHE BOOL "Enable MsQuic logging")
set(CMAKE_BUILD_TYPE "Release")
add_subdirectory(msquic)
target_compile_features(inc INTERFACE cxx_std_20)

# Build msh3 library (and cmd line tool).
add_subdirectory(lib)
if (MSH3_TOOL)
    add_subdirectory(tool)
endif()
if (MSH3_TEST)
    add_subdirectory(test)
endif()
