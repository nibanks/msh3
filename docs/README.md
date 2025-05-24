# MSH3 Documentation

MSH3 is a minimal HTTP/3 library built on top of [MsQuic](https://github.com/microsoft/msquic) and [ls-qpack](https://github.com/litespeedtech/ls-qpack).

## API Documentation

MSH3 provides a C-style API for HTTP/3 functionality. The API is divided into several components:

- [Global API](api/global.md) - Library initialization and version information
- [Configuration](api/configuration.md) - Managing configuration and credentials
- [Connection](api/connection.md) - Managing HTTP/3 connections
- [Request](api/request.md) - Managing HTTP/3 requests and responses
- [Listener](api/listener.md) - Server-side listener for incoming connections
- [Data Structures](api/data-structures.md) - Common data structures used throughout the API

## Getting Started

- [Building MSH3](building.md)
- [Using MSH3 as a Client](client-usage.md)
- [Using MSH3 as a Server](server-usage.md)
- [HTTP/3 and QUIC Protocol Overview](protocol-overview.md)

## Additional Resources

- [HTTP/3 RFC 9114](https://tools.ietf.org/html/rfc9114)
- [QPACK RFC 9204](https://tools.ietf.org/html/rfc9204)
