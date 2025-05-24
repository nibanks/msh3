# msh3

[![Build](https://github.com/nibanks/msh3/actions/workflows/build.yml/badge.svg)](https://github.com/nibanks/msh3/actions/workflows/build.yml)
[![](https://img.shields.io/static/v1?label=RFC&message=9114&color=brightgreen)](https://tools.ietf.org/html/rfc9114)
[![](https://img.shields.io/static/v1?label=RFC&message=9204&color=brightgreen)](https://tools.ietf.org/html/rfc9204)

Minimal HTTP/3 library on top of [microsoft/msquic](https://github.com/microsoft/msquic) and [litespeedtech/ls-qpack](https://github.com/litespeedtech/ls-qpack).

## Features

- Complete HTTP/3 ([RFC 9114](https://tools.ietf.org/html/rfc9114)) implementation
- QPACK header compression ([RFC 9204](https://tools.ietf.org/html/rfc9204)) with dynamic table support
- Client and server support
- Sending and receiving request headers and payload
- Various TLS certificate authentication options
- Optional server validation ("unsecure" mode)

See the [protocol overview](docs/protocol-overview.md) for more information.

# Documentation

Comprehensive documentation is available in the [docs](docs/README.md) folder:

- **API Reference** - Detailed documentation of all APIs and data structures
- **Getting Started** - Guides for building and using MSH3
- **Protocol Overview** - Information about HTTP/3 and QUIC protocols

## Quick Example

```c
// Initialize MSH3 API
MSH3_API* api = MsH3ApiOpen();
if (api) {
    // Create a connection
    MSH3_CONNECTION* connection = MsH3ConnectionOpen(api, ConnectionCallback, context);
    if (connection) {
        // Start the connection to a server
        MsH3ConnectionStart(connection, config, "example.com", &serverAddress);
        
        // Create and send a request
        MSH3_REQUEST* request = MsH3RequestOpen(connection, RequestCallback, context, MSH3_REQUEST_FLAG_NONE);
        if (request) {
            // Send headers and optional data
            MsH3RequestSend(request, MSH3_REQUEST_SEND_FLAG_FIN, headers, headerCount, body, bodyLength, context);
            
            // Clean up when done
            MsH3RequestClose(request);
        }
        MsH3ConnectionClose(connection);
    }
    MsH3ApiClose(api);
}
```

See the [client usage guide](docs/client-usage.md) and [server usage guide](docs/server-usage.md) for detailed examples.

# Building

For detailed build instructions, see the [building guide](docs/building.md).

Quick start:

```bash
git clone --recursive https://github.com/nibanks/msh3
cd msh3 && mkdir build && cd build

# Linux
cmake -G 'Unix Makefiles' ..
cmake --build .

# Windows
cmake -G 'Visual Studio 17 2022' -A x64 ..
cmake --build .
```

# Running the Sample Application

```bash
# Simple HTTP/3 GET requests to various servers
msh3app outlook.office.com
msh3app www.cloudflare.com
msh3app www.google.com
msh3app nghttp2.org:4433
```
