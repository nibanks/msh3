# Building MSH3

This guide explains how to build MSH3 from source on different platforms.

## Prerequisites

- CMake 3.16 or higher
- A C++ compiler with C++14 support
- Git (for cloning the repository and its dependencies)

## Getting the Source Code

```bash
git clone --recursive https://github.com/nibanks/msh3
cd msh3
```

If you've already cloned the repository without the `--recursive` flag, you can fetch the submodules separately:

```bash
git submodule update --init --recursive
```

## Building on Linux

```bash
mkdir build && cd build
cmake -G 'Unix Makefiles' ..
cmake --build .
```

To build with specific compiler flags:

```bash
cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-O3" -DCMAKE_CXX_FLAGS="-O3" ..
cmake --build .
```

## Building on Windows

Using Visual Studio:

```bash
mkdir build && cd build
cmake -G 'Visual Studio 17 2022' -A x64 ..
cmake --build .
```

Using Ninja:

```bash
mkdir build && cd build
cmake -G 'Ninja' ..
cmake --build .
```

## Building on macOS

```bash
mkdir build && cd build
cmake -G 'Unix Makefiles' ..
cmake --build .
```

## Install

To install MSH3 on your system:

```bash
cmake --build . --target install
```

You might need elevated privileges (sudo on Linux/macOS, or Administrator on Windows) to install to system directories.

## Using MSH3 in Your Project

### With CMake

```cmake
find_package(msh3 REQUIRED)
target_link_libraries(your_target_name PRIVATE msh3::msh3)
```

### With pkg-config

```bash
pkg-config --cflags --libs libmsh3
```
