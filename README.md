# msh3

Minimal HTTP/3 client on top of [microsoft/msquic](https://github.com/microsoft/msquic) and [litespeedtech/ls-qpack](https://github.com/litespeedtech/ls-qpack).

# Build

```
git clone https://github.com/nibanks/msh3
git submodule update --init --recursive
cd msh3
mkdir build && cd build
```

## Windows
```
cmake -G 'Visual Studio 17 2022' -A x64 ..
cmake --build .
```

## Linux
```
cmake -G 'Unix Makefiles' ..
cmake --build .
```

# Run

```
msh3app cloudflare.com index.html
```
