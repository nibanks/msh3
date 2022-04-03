# msh3

[![Build](https://github.com/nibanks/msh3/actions/workflows/build.yml/badge.svg)](https://github.com/nibanks/msh3/actions/workflows/build.yml)

Minimal HTTP/3 client on top of [microsoft/msquic](https://github.com/microsoft/msquic) and [litespeedtech/ls-qpack](https://github.com/litespeedtech/ls-qpack). Currently supports:

- Sending request headers.
- Receiving response headers and payload.
- Static qpack encoding.
- Server validation can be optionally disabled ("unsecure" mode).

# API

```c
const MSH3_REQUEST_IF Callbacks = { HeaderReceived, DataReceived, Complete, Shutdown };

const MSH3_HEADER Headers[] = {
    { ":method", 7, "GET", 3 },
    { ":path", 5, Path, strlen(Path) },
    { ":scheme", 7, "https", 5 },
    { ":authority", 10, Host, strlen(Host) },
    { "accept", 6, "*/*", 3 },
};
const size_t HeadersCount = sizeof(Headers)/sizeof(MSH3_HEADER);

MSH3_API* Api = MsH3ApiOpen();
if (Api) {
    MSH3_CONNECTION* Connection = MsH3ConnectionOpen(Api, Host, Unsecure);
    if (Connection) {
        MSH3_REQUEST* Request = MsH3RequestOpen(Connection, &Callbacks, NULL, Headers, HeadersCount);
        // ...
        MsH3ConnectionClose(Connection);
    }
    MsH3ApiClose(Api);
}
```

# Build

```Bash
git clone --recursive https://github.com/nibanks/msh3
cd msh3 && mkdir build && cd build
```

### Linux
```Bash
cmake -G 'Unix Makefiles' ..
cmake --build .
```

### Windows
```Bash
cmake -G 'Visual Studio 17 2022' -A x64 ..
cmake --build .
```

# Run

```
msh3app outlook.office.com
msh3app www.cloudflare.com
msh3app www.google.com
```
