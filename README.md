# msh3

Minimal HTTP/3 client on top of [microsoft/msquic](https://github.com/microsoft/msquic) and [litespeedtech/ls-qpack](https://github.com/litespeedtech/ls-qpack).

# API

```c
const MSH3_REQUEST_IF Callbacks = { HeaderReceived, DataReceived, Complete, Shutdown };

MSH3_API* Api = MsH3ApiOpen();
if (Api) {
    MSH3_CONNECTION* Connection = MsH3ConnectionOpen(Api, Host, Secure);
    if (Connection) {
        MsH3RequestOpen(Connection, &Callbacks, NULL, Path);
        MsH3ConnectionClose(Connection);
    }
    MsH3ApiClose(Api);
}
```

# Build

```
git clone https://github.com/nibanks/msh3
git submodule update --init --recursive
cd msh3
mkdir build && cd build
```

## Linux
```
cmake -G 'Unix Makefiles' ..
cmake --build .
```

## Windows
```
cmake -G 'Visual Studio 17 2022' -A x64 ..
cmake --build .
```

# Run

```
msh3app www.cloudflare.com
msh3app www.google.com /index.html
```
