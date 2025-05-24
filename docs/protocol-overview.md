# HTTP/3 and QUIC Protocol Overview

This document provides a high-level overview of the HTTP/3 and QUIC protocols that power the MSH3 library.

## QUIC Protocol (RFC 9000)

QUIC is a modern, encrypted transport protocol designed for the modern internet. It was developed to address the limitations of TCP and TLS for web applications.

### Key Features of QUIC

1. **Multiplexing**: QUIC supports multiple simultaneous streams over a single connection, eliminating head-of-line blocking at the transport layer.

2. **Encryption by Default**: All QUIC packets, except the initial handshake packets, are encrypted. This provides privacy and security for all transported data.

3. **Connection Migration**: QUIC connections can survive network changes (like switching from Wi-Fi to cellular) without breaking.

4. **Reduced Connection Establishment Time**: QUIC combines the cryptographic and transport handshakes, reducing the round trips needed to establish a secure connection.

5. **Improved Congestion Control**: QUIC includes modern congestion control mechanisms and better loss recovery than TCP.

6. **Forward Error Correction**: QUIC can use forward error correction to recover from packet loss without waiting for retransmissions.

7. **Connection IDs**: QUIC uses connection IDs rather than IP address/port tuples to identify connections, enabling connection migration.

## HTTP/3 Protocol (RFC 9114)

HTTP/3 is the latest version of the HTTP protocol, designed to use QUIC as its transport protocol.

### Key Features of HTTP/3

1. **Multiplexed Streams**: HTTP/3 requests and responses are sent over QUIC streams, eliminating head-of-line blocking issues present in HTTP/2.

2. **Header Compression**: HTTP/3 uses QPACK (RFC 9204) for header compression, which is designed to work efficiently with QUIC's out-of-order delivery. QPACK supports both static and dynamic table compression, allowing for efficient compression of repeated headers across requests.

3. **Server Push**: Like HTTP/2, HTTP/3 supports server push, allowing servers to proactively send resources to clients.

4. **Stream Priorities**: HTTP/3 includes a priority scheme for streams, allowing clients to specify which resources should be delivered first.

5. **Improved Performance**: By leveraging QUIC's features, HTTP/3 provides better performance especially on networks with high latency or packet loss.

### HTTP/3 Frame Types

HTTP/3 defines several frame types for different purposes:

- `DATA`: Contains the message body
- `HEADERS`: Contains HTTP header fields
- `CANCEL_PUSH`: Used to cancel server push
- `SETTINGS`: Conveys configuration parameters
- `PUSH_PROMISE`: Used for server push
- `GOAWAY`: Indicates the end of a connection
- `MAX_PUSH_ID`: Controls the number of server pushes
- `RESERVED`: Reserved for future use

## QPACK Header Compression (RFC 9204)

QPACK is the header compression format used in HTTP/3, designed to work well with QUIC's out-of-order delivery.

### Key Features of QPACK

1. **Static and Dynamic Tables**: QPACK uses both static and dynamic tables to achieve compression, similar to HPACK in HTTP/2.

2. **Encoder Stream**: The encoder stream is used to update the dynamic table at the decoder.

3. **Decoder Stream**: The decoder stream is used to acknowledge dynamic table updates and provide flow control.

4. **Blocking Avoidance**: QPACK is designed to minimize head-of-line blocking issues when dynamic table updates are in flight.

## How MSH3 Implements These Protocols

MSH3 implements HTTP/3 on top of Microsoft's implementation of QUIC (MsQuic) and LiteSpeed's implementation of QPACK (ls-qpack).

- **MsQuic**: Provides the QUIC transport functionality, including connection establishment, stream management, and encrypted transport.

- **ls-qpack**: Provides the QPACK header compression and decompression functionality.

- **MSH3**: Ties these components together to provide a complete HTTP/3 implementation, adding the HTTP/3 framing layer, request/response handling, and an API for applications to use.

## HTTP/3 Request and Response Flow

1. The client establishes a QUIC connection to the server.

2. The client creates a new bidirectional QUIC stream for each HTTP request.

3. The client sends HEADERS frames containing the request headers, and optionally DATA frames containing the request body.

4. The server processes the request and sends back HEADERS frames containing the response headers, and optionally DATA frames containing the response body.

5. The stream is closed when the response is complete.

## Performance Considerations

- **0-RTT**: QUIC supports 0-RTT (zero round trip time) connection establishment, allowing clients to send data on the first packet of a connection if they've connected to the server before.

- **Connection Coalescing**: Multiple HTTP/3 connections to the same server can be coalesced into a single QUIC connection, reducing overhead.

- **Early Data**: QUIC supports sending application data during the handshake, reducing latency for certain types of requests.

- **Flow Control**: QUIC and HTTP/3 include flow control mechanisms at both the connection and stream levels.

## Security Considerations

- **TLS 1.3**: QUIC uses TLS 1.3 for encryption and authentication, providing modern cryptographic security.

- **Always Encrypted**: All HTTP/3 traffic is encrypted by default.

- **Authentication**: QUIC supports mutual authentication using certificates.

- **Privacy**: QUIC's encrypted transport helps protect against traffic analysis and fingerprinting.

## References

- [RFC 9000: QUIC: A UDP-Based Multiplexed and Secure Transport](https://tools.ietf.org/html/rfc9000)
- [RFC 9001: Using TLS to Secure QUIC](https://tools.ietf.org/html/rfc9001)
- [RFC 9002: QUIC Loss Detection and Congestion Control](https://tools.ietf.org/html/rfc9002)
- [RFC 9114: HTTP/3](https://tools.ietf.org/html/rfc9114)
- [RFC 9204: QPACK: Field Compression for HTTP/3](https://tools.ietf.org/html/rfc9204)
