# MSH3 Data Structures

This document provides an overview of the key data structures used in the MSH3 API.

## MSH3_SETTINGS

```c
typedef struct MSH3_SETTINGS {
    union {
        uint64_t IsSetFlags;
        struct {
            uint64_t IdleTimeoutMs                          : 1;
            uint64_t DisconnectTimeoutMs                    : 1;
            uint64_t KeepAliveIntervalMs                    : 1;
            uint64_t InitialRttMs                           : 1;
            uint64_t PeerRequestCount                       : 1;
            uint64_t DatagramEnabled                        : 1;
#ifdef MSH3_API_ENABLE_PREVIEW_FEATURES
            uint64_t XdpEnabled                             : 1;
#endif
        } IsSet;
    };
    uint64_t IdleTimeoutMs;
    uint32_t DisconnectTimeoutMs;
    uint32_t KeepAliveIntervalMs;
    uint32_t InitialRttMs;
    uint16_t PeerRequestCount;
    uint8_t DatagramEnabled : 1; // TODO - Add flags instead?
#ifdef MSH3_API_ENABLE_PREVIEW_FEATURES
    uint8_t XdpEnabled : 1;
    uint8_t RESERVED : 6;
#else
    uint8_t RESERVED : 7;
#endif
} MSH3_SETTINGS;
```

The `MSH3_SETTINGS` structure is used to configure various aspects of MSH3 behavior.

- `IsSetFlags` / `IsSet`: A union that determines which settings are active. Each bit corresponds to a specific setting.
- `IdleTimeoutMs`: The idle timeout in milliseconds.
- `DisconnectTimeoutMs`: The disconnect timeout in milliseconds.
- `KeepAliveIntervalMs`: The keep alive interval in milliseconds.
- `InitialRttMs`: The initial round trip time estimate in milliseconds.
- `PeerRequestCount`: The maximum number of requests allowed from a peer.
- `DatagramEnabled`: Flag to enable QUIC datagrams.
- `XdpEnabled`: Flag to enable XDP (available only when preview features are enabled).

## MSH3_ADDR

```c
typedef union MSH3_ADDR {
    struct sockaddr Ip;
    struct sockaddr_in Ipv4;
    struct sockaddr_in6 Ipv6;
} MSH3_ADDR;
```

The `MSH3_ADDR` union represents an IP address, supporting both IPv4 and IPv6.

- `Ip`: Generic socket address.
- `Ipv4`: IPv4 socket address.
- `Ipv6`: IPv6 socket address.

A helper macro `MSH3_SET_PORT` is provided to set the port in a portable way.

## MSH3_HEADER

```c
typedef struct MSH3_HEADER {
    const char* Name;
    size_t NameLength;
    const char* Value;
    size_t ValueLength;
} MSH3_HEADER;
```

The `MSH3_HEADER` structure represents an HTTP header.

- `Name`: Pointer to the header name.
- `NameLength`: Length of the header name.
- `Value`: Pointer to the header value.
- `ValueLength`: Length of the header value.

## MSH3_CREDENTIAL_CONFIG

```c
typedef struct MSH3_CREDENTIAL_CONFIG {
    MSH3_CREDENTIAL_TYPE Type;
    MSH3_CREDENTIAL_FLAGS Flags;
    union {
        MSH3_CERTIFICATE_HASH* CertificateHash;
        MSH3_CERTIFICATE_HASH_STORE* CertificateHashStore;
        MSH3_CERTIFICATE_CONTEXT* CertificateContext;
        MSH3_CERTIFICATE_FILE* CertificateFile;
        MSH3_CERTIFICATE_FILE_PROTECTED* CertificateFileProtected;
        MSH3_CERTIFICATE_PKCS12* CertificatePkcs12;
    };
} MSH3_CREDENTIAL_CONFIG;
```

The `MSH3_CREDENTIAL_CONFIG` structure is used to configure TLS credentials.

- `Type`: The type of credential to use.
- `Flags`: Flags to control credential behavior.
- Union containing a pointer to the specific credential type based on `Type`.

### Credential Types

```c
typedef enum MSH3_CREDENTIAL_TYPE {
    MSH3_CREDENTIAL_TYPE_NONE,
    MSH3_CREDENTIAL_TYPE_CERTIFICATE_HASH,
    MSH3_CREDENTIAL_TYPE_CERTIFICATE_HASH_STORE,
    MSH3_CREDENTIAL_TYPE_CERTIFICATE_CONTEXT,
    MSH3_CREDENTIAL_TYPE_CERTIFICATE_FILE,
    MSH3_CREDENTIAL_TYPE_CERTIFICATE_FILE_PROTECTED,
    MSH3_CREDENTIAL_TYPE_CERTIFICATE_PKCS12,
#ifdef MSH3_TEST_MODE
    MSH3_CREDENTIAL_TYPE_SELF_SIGNED_CERTIFICATE,
#endif // MSH3_TEST_MODE
} MSH3_CREDENTIAL_TYPE;
```

### Credential Flags

```c
typedef enum MSH3_CREDENTIAL_FLAGS {
    MSH3_CREDENTIAL_FLAG_NONE                           = 0x00000000,
    MSH3_CREDENTIAL_FLAG_CLIENT                         = 0x00000001, // Lack of client flag indicates server.
    MSH3_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION      = 0x00000002,
    MSH3_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION  = 0x00000004,
} MSH3_CREDENTIAL_FLAGS;
```

## Event Structures

### MSH3_CONNECTION_EVENT

```c
typedef struct MSH3_CONNECTION_EVENT {
    MSH3_CONNECTION_EVENT_TYPE Type;
    union {
        struct {
            MSH3_STATUS Status;
            uint64_t ErrorCode; // Wire format error code.
        } SHUTDOWN_INITIATED_BY_TRANSPORT;
        struct {
            uint64_t ErrorCode;
        } SHUTDOWN_INITIATED_BY_PEER;
        struct {
            bool HandshakeCompleted          : 1;
            bool PeerAcknowledgedShutdown    : 1;
            bool AppCloseInProgress          : 1;
        } SHUTDOWN_COMPLETE;
        struct {
            MSH3_REQUEST* Request;
        } NEW_REQUEST;
    };
} MSH3_CONNECTION_EVENT;
```

Connection event types:

```c
typedef enum MSH3_CONNECTION_EVENT_TYPE {
    MSH3_CONNECTION_EVENT_SHUTDOWN_COMPLETE                 = 0,    // Ready for the handle to be closed.
    MSH3_CONNECTION_EVENT_CONNECTED                         = 1,
    MSH3_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT   = 2,    // The transport started the shutdown process.
    MSH3_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER        = 3,    // The peer application started the shutdown process.
    MSH3_CONNECTION_EVENT_NEW_REQUEST                       = 4,
} MSH3_CONNECTION_EVENT_TYPE;
```

### MSH3_REQUEST_EVENT

```c
typedef struct MSH3_REQUEST_EVENT {
    MSH3_REQUEST_EVENT_TYPE Type;
    union {
        struct {
            bool ConnectionShutdown;
            bool AppCloseInProgress       : 1;
            bool ConnectionShutdownByApp  : 1;
            bool ConnectionClosedRemotely : 1;
            bool RESERVED                 : 1;
            bool RESERVED_2               : 1;
            bool RESERVED_3               : 1;
            bool RESERVED_4               : 1;
            bool RESERVED_5               : 1;
            uint64_t ConnectionErrorCode;
            MSH3_STATUS ConnectionCloseStatus;
        } SHUTDOWN_COMPLETE;
        struct {
            const MSH3_HEADER* Header;
        } HEADER_RECEIVED;
        struct {
            uint32_t Length;
            const uint8_t* Data;
        } DATA_RECEIVED;
        struct {
            uint64_t ErrorCode;
        } PEER_SEND_ABORTED;
        struct {
            uint64_t ByteCount;
        } IDEAL_SEND_SIZE;
        struct {
            bool Canceled;
            void* ClientContext;
        } SEND_COMPLETE;
        struct {
            bool Graceful;
        } SEND_SHUTDOWN_COMPLETE;
        struct {
            uint64_t ErrorCode;
        } PEER_RECEIVE_ABORTED;
    };
} MSH3_REQUEST_EVENT;
```

Request event types:

```c
typedef enum MSH3_REQUEST_EVENT_TYPE {
    MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE                 = 0,    // Ready for the handle to be closed.
    MSH3_REQUEST_EVENT_HEADER_RECEIVED                   = 1,
    MSH3_REQUEST_EVENT_DATA_RECEIVED                     = 2,
    MSH3_REQUEST_EVENT_PEER_SEND_SHUTDOWN                = 3,
    MSH3_REQUEST_EVENT_PEER_SEND_ABORTED                 = 4,
    MSH3_REQUEST_EVENT_IDEAL_SEND_SIZE                   = 5,
    MSH3_REQUEST_EVENT_SEND_COMPLETE                     = 6,
    MSH3_REQUEST_EVENT_SEND_SHUTDOWN_COMPLETE            = 7,
    MSH3_REQUEST_EVENT_PEER_RECEIVE_ABORTED              = 8,
} MSH3_REQUEST_EVENT_TYPE;
```

### MSH3_LISTENER_EVENT

```c
typedef struct MSH3_LISTENER_EVENT {
    MSH3_LISTENER_EVENT_TYPE Type;
    union {
        struct {
            bool AppCloseInProgress  : 1;
            bool RESERVED            : 1;
            bool RESERVED_2          : 1;
            bool RESERVED_3          : 1;
            bool RESERVED_4          : 1;
            bool RESERVED_5          : 1;
            bool RESERVED_6          : 1;
            bool RESERVED_7          : 1;
        } SHUTDOWN_COMPLETE;
        struct {
            MSH3_CONNECTION* Connection;
            const char* ServerName;
            uint16_t ServerNameLength;
        } NEW_CONNECTION;
    };
} MSH3_LISTENER_EVENT;
```

Listener event types:

```c
typedef enum MSH3_LISTENER_EVENT_TYPE {
    MSH3_LISTENER_EVENT_SHUTDOWN_COMPLETE                 = 0,    // Ready for the handle to be closed.
    MSH3_LISTENER_EVENT_NEW_CONNECTION                    = 1,
} MSH3_LISTENER_EVENT_TYPE;
```

## Flag Enumerations

### MSH3_REQUEST_FLAGS

```c
typedef enum MSH3_REQUEST_FLAGS {
    MSH3_REQUEST_FLAG_NONE                              = 0x0000,
    MSH3_REQUEST_FLAG_ALLOW_0_RTT                       = 0x0001,   // Allows the use of encrypting with 0-RTT key.
} MSH3_REQUEST_FLAGS;
```

### MSH3_REQUEST_SEND_FLAGS

```c
typedef enum MSH3_REQUEST_SEND_FLAGS {
    MSH3_REQUEST_SEND_FLAG_NONE                         = 0x0000,
    MSH3_REQUEST_SEND_FLAG_ALLOW_0_RTT                  = 0x0001,   // Allows the use of encrypting with 0-RTT key.
    MSH3_REQUEST_SEND_FLAG_FIN                          = 0x0002,   // Indicates the request should be gracefully shutdown too.
    MSH3_REQUEST_SEND_FLAG_DELAY_SEND                   = 0x0004,   // Indicates the send should be delayed because more will be queued soon.
} MSH3_REQUEST_SEND_FLAGS;
```

### MSH3_REQUEST_SHUTDOWN_FLAGS

```c
typedef enum MSH3_REQUEST_SHUTDOWN_FLAGS {
    MSH3_REQUEST_SHUTDOWN_FLAG_NONE                     = 0x0000,
    MSH3_REQUEST_SHUTDOWN_FLAG_GRACEFUL                 = 0x0001,   // Cleanly closes the send path.
    MSH3_REQUEST_SHUTDOWN_FLAG_ABORT_SEND               = 0x0002,   // Abruptly closes the send path.
    MSH3_REQUEST_SHUTDOWN_FLAG_ABORT_RECEIVE            = 0x0004,   // Abruptly closes the receive path.
    MSH3_REQUEST_SHUTDOWN_FLAG_ABORT                    = 0x0006,   // Abruptly closes both send and receive paths.
} MSH3_REQUEST_SHUTDOWN_FLAGS;
```
