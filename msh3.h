/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#ifndef _MSH3_
#define _MSH3_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#define SUCCESS_HRESULT_FROM_WIN32(x) \
    ((HRESULT)(((x) & 0x0000FFFF) | (FACILITY_WIN32 << 16)))
#define MSH3_CALL __cdecl
#define MSH3_STATUS HRESULT
#define MSH3_STATUS_SUCCESS         S_OK
#define MSH3_STATUS_PENDING         SUCCESS_HRESULT_FROM_WIN32(ERROR_IO_PENDING)
#define MSH3_STATUS_INVALID_STATE   E_NOT_VALID_STATE
#define MSH3_FAILED(X) FAILED(X)
typedef HANDLE MSH3_EVENTQ;
typedef OVERLAPPED_ENTRY MSH3_CQE;
typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
void
(MSH3_EVENT_COMPLETION)(
    _In_ MSH3_CQE* Cqe
    );
typedef MSH3_EVENT_COMPLETION *MSH3_EVENT_COMPLETION_HANDLER;
typedef struct MSH3_SQE {
    OVERLAPPED Overlapped;
    MSH3_EVENT_COMPLETION_HANDLER Completion;
#if DEBUG
    BOOLEAN IsQueued; // Debug flag to catch double queueing.
#endif
} MSH3_SQE;
#else
#include <netinet/ip.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#define MSH3_CALL
#define MSH3_STATUS unsigned int
#define MSH3_STATUS_SUCCESS         ((MSH3_STATUS)0)
#define MSH3_STATUS_PENDING         ((MSH3_STATUS)-2)
#define MSH3_STATUS_INVALID_STATE   ((MSH3_STATUS)EPERM)
#define MSH3_FAILED(X) ((int)(X) > 0)
#if __linux__ // epoll
#include <sys/epoll.h>
#include <sys/eventfd.h>
typedef int MSH3_EVENTQ;
typedef struct epoll_event MSH3_CQE;
typedef
void
(MSH3_EVENT_COMPLETION)(
    MSH3_CQE* Cqe
    );
typedef MSH3_EVENT_COMPLETION *MSH3_EVENT_COMPLETION_HANDLER;
typedef struct MSH3_SQE {
    int fd;
    MSH3_EVENT_COMPLETION_HANDLER Completion;
} MSH3_SQE;
#elif __APPLE__ || __FreeBSD__ // kqueue
#include <sys/event.h>
#include <fcntl.h>
typedef int MSH3_EVENTQ;
typedef struct kevent MSH3_CQE;
typedef
void
(MSH3_EVENT_COMPLETION)(
    MSH3_CQE* Cqe
    );
typedef MSH3_EVENT_COMPLETION *MSH3_EVENT_COMPLETION_HANDLER;
typedef struct MSH3_SQE {
    uintptr_t Handle;
    MSH3_EVENT_COMPLETION_HANDLER Completion;
} MSH3_SQE;
#endif // __linux__
#ifndef DEFINE_ENUM_FLAG_OPERATORS
#ifdef __cplusplus
extern "C++" {
template <size_t S> struct _ENUM_FLAG_INTEGER_FOR_SIZE;
template <> struct _ENUM_FLAG_INTEGER_FOR_SIZE<1> { typedef uint8_t type; };
template <> struct _ENUM_FLAG_INTEGER_FOR_SIZE<2> { typedef uint16_t type; };
template <> struct _ENUM_FLAG_INTEGER_FOR_SIZE<4> { typedef uint32_t type; };
template <> struct _ENUM_FLAG_INTEGER_FOR_SIZE<8> { typedef uint64_t type; };
// used as an approximation of std::underlying_type<T>
template <class T> struct _ENUM_FLAG_SIZED_INTEGER {
    typedef typename _ENUM_FLAG_INTEGER_FOR_SIZE<sizeof(T)>::type type;
};
}
#define DEFINE_ENUM_FLAG_OPERATORS(ENUMTYPE) \
extern "C++" { \
inline ENUMTYPE operator | (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)a) | ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b)); } \
inline ENUMTYPE &operator |= (ENUMTYPE &a, ENUMTYPE b) throw() { return (ENUMTYPE &)(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type &)a) |= ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b)); } \
inline ENUMTYPE operator & (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)a) & ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b)); } \
inline ENUMTYPE &operator &= (ENUMTYPE &a, ENUMTYPE b) throw() { return (ENUMTYPE &)(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type &)a) &= ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b)); } \
inline ENUMTYPE operator ~ (ENUMTYPE a) throw() { return ENUMTYPE(~((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)a)); } \
inline ENUMTYPE operator ^ (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)a) ^ ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b)); } \
inline ENUMTYPE &operator ^= (ENUMTYPE &a, ENUMTYPE b) throw() { return (ENUMTYPE &)(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type &)a) ^= ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b)); } \
}
#else
#define DEFINE_ENUM_FLAG_OPERATORS(ENUMTYPE) // NOP, C allows these operators.
#endif
#endif // DEFINE_ENUM_FLAG_OPERATORS
#endif


#if defined(__cplusplus)
extern "C" {
#endif

typedef struct MSH3_API MSH3_API;
typedef struct MSH3_CONFIGURATION MSH3_CONFIGURATION;
typedef struct MSH3_CONNECTION MSH3_CONNECTION;
typedef struct MSH3_REQUEST MSH3_REQUEST;
typedef struct MSH3_LISTENER MSH3_LISTENER;

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

typedef enum MSH3_CREDENTIAL_FLAGS {
    MSH3_CREDENTIAL_FLAG_NONE                           = 0x00000000,
    MSH3_CREDENTIAL_FLAG_CLIENT                         = 0x00000001, // Lack of client flag indicates server.
    MSH3_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION      = 0x00000002,
    MSH3_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION  = 0x00000004,
} MSH3_CREDENTIAL_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(MSH3_CREDENTIAL_FLAGS)

typedef enum MSH3_CERTIFICATE_HASH_STORE_FLAGS {
    MSH3_CERTIFICATE_HASH_STORE_FLAG_NONE               = 0x0000,
    MSH3_CERTIFICATE_HASH_STORE_FLAG_MACHINE_STORE      = 0x0001,
} MSH3_CERTIFICATE_HASH_STORE_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(MSH3_CERTIFICATE_HASH_STORE_FLAGS)

typedef enum MSH3_REQUEST_FLAGS {
    MSH3_REQUEST_FLAG_NONE                              = 0x0000,
    MSH3_REQUEST_FLAG_ALLOW_0_RTT                       = 0x0001,   // Allows the use of encrypting with 0-RTT key.
} MSH3_REQUEST_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(MSH3_REQUEST_FLAGS)

typedef enum MSH3_REQUEST_SEND_FLAGS {
    MSH3_REQUEST_SEND_FLAG_NONE                         = 0x0000,
    MSH3_REQUEST_SEND_FLAG_ALLOW_0_RTT                  = 0x0001,   // Allows the use of encrypting with 0-RTT key.
    MSH3_REQUEST_SEND_FLAG_FIN                          = 0x0002,   // Indicates the request should be gracefully shutdown too.
    MSH3_REQUEST_SEND_FLAG_DELAY_SEND                   = 0x0004,   // Indicates the send should be delayed because more will be queued soon.
} MSH3_REQUEST_SEND_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(MSH3_REQUEST_SEND_FLAGS)

typedef enum MSH3_REQUEST_SHUTDOWN_FLAGS {
    MSH3_REQUEST_SHUTDOWN_FLAG_NONE                     = 0x0000,
    MSH3_REQUEST_SHUTDOWN_FLAG_GRACEFUL                 = 0x0001,   // Cleanly closes the send path.
    MSH3_REQUEST_SHUTDOWN_FLAG_ABORT_SEND               = 0x0002,   // Abruptly closes the send path.
    MSH3_REQUEST_SHUTDOWN_FLAG_ABORT_RECEIVE            = 0x0004,   // Abruptly closes the receive path.
    MSH3_REQUEST_SHUTDOWN_FLAG_ABORT                    = 0x0006,   // Abruptly closes both send and receive paths.
} MSH3_REQUEST_SHUTDOWN_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(MSH3_REQUEST_SHUTDOWN_FLAGS)

typedef struct MSH3_EXECUTION_CONFIG {
    uint32_t IdealProcessor;
    MSH3_EVENTQ* EventQ;
} MSH3_EXECUTION_CONFIG;

typedef struct MSH3_EXECUTION MSH3_EXECUTION;

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

typedef struct MSH3_CERTIFICATE_HASH {
    uint8_t ShaHash[20];
} MSH3_CERTIFICATE_HASH;

typedef struct MSH3_CERTIFICATE_HASH_STORE {
    MSH3_CERTIFICATE_HASH_STORE_FLAGS Flags;
    uint8_t ShaHash[20];
    char StoreName[128];
} MSH3_CERTIFICATE_HASH_STORE;

typedef void MSH3_CERTIFICATE_CONTEXT;

typedef struct MSH3_CERTIFICATE_FILE {
    const char *PrivateKeyFile;
    const char *CertificateFile;
} MSH3_CERTIFICATE_FILE;

typedef struct MSH3_CERTIFICATE_FILE_PROTECTED {
    const char *PrivateKeyFile;
    const char *CertificateFile;
    const char *PrivateKeyPassword;
} MSH3_CERTIFICATE_FILE_PROTECTED;

typedef struct MSH3_CERTIFICATE_PKCS12 {
    const uint8_t *Asn1Blob;
    uint32_t Asn1BlobLength;
    const char *PrivateKeyPassword;     // Optional
} MSH3_CERTIFICATE_PKCS12;

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

typedef union MSH3_ADDR {
    struct sockaddr Ip;
    struct sockaddr_in Ipv4;
    struct sockaddr_in6 Ipv6;
} MSH3_ADDR;

#ifdef _WIN32
#define MSH3_SET_PORT(addr, port) (addr)->Ipv4.sin_port = _byteswap_ushort(port)
#else
#define MSH3_SET_PORT(addr, port) (addr)->Ipv4.sin_port = __builtin_bswap16(port)
#endif

typedef struct MSH3_HEADER {
    const char* Name;
    size_t NameLength;
    const char* Value;
    size_t ValueLength;
} MSH3_HEADER;

//
// API global interface
//

void
MSH3_CALL
MsH3Version(
    uint32_t Version[4]
    );

MSH3_API*
MSH3_CALL
MsH3ApiOpen(
    void
    );

#ifdef MSH3_API_ENABLE_PREVIEW_FEATURES
MSH3_API*
MSH3_CALL
MsH3ApiOpenWithExecution(
    uint32_t ExecutionConfigCount,
    MSH3_EXECUTION_CONFIG* ExecutionConfigs,
    MSH3_EXECUTION** Executions
    );

uint32_t
MSH3_CALL
MsH3ApiPoll(
    _In_ MSH3_EXECUTION* Execution
    );
#endif

void
MSH3_CALL
MsH3ApiClose(
    MSH3_API* Api
    );

//
// Configuration interface
//

MSH3_CONFIGURATION*
MSH3_CALL
MsH3ConfigurationOpen(
    MSH3_API* Api,
    const MSH3_SETTINGS* Settings, // optional
    uint32_t SettingsLength
    );

MSH3_STATUS
MSH3_CALL
MsH3ConfigurationLoadCredential(
    MSH3_CONFIGURATION* Configuration,
    const MSH3_CREDENTIAL_CONFIG* CredentialConfig
    );

void
MSH3_CALL
MsH3ConfigurationClose(
    MSH3_CONFIGURATION* Configuration
    );

//
// Connection interface
//

typedef enum MSH3_CONNECTION_EVENT_TYPE {
    MSH3_CONNECTION_EVENT_SHUTDOWN_COMPLETE                 = 0,    // Ready for the handle to be closed.
    MSH3_CONNECTION_EVENT_CONNECTED                         = 1,
    MSH3_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT   = 2,    // The transport started the shutdown process.
    MSH3_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER        = 3,    // The peer application started the shutdown process.
    MSH3_CONNECTION_EVENT_NEW_REQUEST                       = 4,
    // Future events may be added. Existing code should
    // return NOT_SUPPORTED for any unknown event.
} MSH3_CONNECTION_EVENT_TYPE;

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

typedef
MSH3_STATUS
(MSH3_CALL MSH3_CONNECTION_CALLBACK)(
    MSH3_CONNECTION* Connection,
    void* Context,
    MSH3_CONNECTION_EVENT* Event
    );

typedef MSH3_CONNECTION_CALLBACK *MSH3_CONNECTION_CALLBACK_HANDLER;

MSH3_CONNECTION*
MSH3_CALL
MsH3ConnectionOpen(
    MSH3_API* Api,
    const MSH3_CONNECTION_CALLBACK_HANDLER Handler,
    void* Context
    );

void
MSH3_CALL
MsH3ConnectionSetCallbackHandler(
    MSH3_CONNECTION* Connection,
    const MSH3_CONNECTION_CALLBACK_HANDLER Handler,
    void* Context
    );

MSH3_STATUS
MSH3_CALL
MsH3ConnectionSetConfiguration(
    MSH3_CONNECTION* Connection,
    MSH3_CONFIGURATION* Configuration
    );

MSH3_STATUS
MSH3_CALL
MsH3ConnectionStart(
    MSH3_CONNECTION* Connection,
    MSH3_CONFIGURATION* Configuration,
    const char* ServerName,
    const MSH3_ADDR* ServerAddress
    );

void
MSH3_CALL
MsH3ConnectionShutdown(
    MSH3_CONNECTION* Connection,
    uint64_t ErrorCode
    );

void
MSH3_CALL
MsH3ConnectionClose(
    MSH3_CONNECTION* Connection
    );

//
// Request Interface
//

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
    // Future events may be added. Existing code should
    // return NOT_SUPPORTED for any unknown event.
} MSH3_REQUEST_EVENT_TYPE;

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

typedef
MSH3_STATUS
(MSH3_CALL MSH3_REQUEST_CALLBACK)(
    MSH3_REQUEST* Request,
    void* Context,
    MSH3_REQUEST_EVENT* Event
    );

typedef MSH3_REQUEST_CALLBACK *MSH3_REQUEST_CALLBACK_HANDLER;

MSH3_REQUEST*
MSH3_CALL
MsH3RequestOpen(
    MSH3_CONNECTION* Connection,
    const MSH3_REQUEST_CALLBACK_HANDLER Handler,
    void* Context,
    MSH3_REQUEST_FLAGS Flags
    );

void
MSH3_CALL
MsH3RequestSetCallbackHandler(
    MSH3_REQUEST* Request,
    const MSH3_REQUEST_CALLBACK_HANDLER Handler,
    void* Context
    );

bool
MSH3_CALL
MsH3RequestSend(
    MSH3_REQUEST* Request,
    MSH3_REQUEST_SEND_FLAGS Flags,
    const MSH3_HEADER* Headers,
    size_t HeadersCount,
    const void* Data,
    uint32_t DataLength,
    void* AppContext
    );

void
MSH3_CALL
MsH3RequestSetReceiveEnabled(
    MSH3_REQUEST* Request,
    bool Enabled
    );

void
MSH3_CALL
MsH3RequestCompleteReceive(
    MSH3_REQUEST* Request,
    uint32_t Length
    );

void
MSH3_CALL
MsH3RequestShutdown(
    MSH3_REQUEST* Request,
    MSH3_REQUEST_SHUTDOWN_FLAGS Flags,
    uint64_t AbortError // Only for MSH3_REQUEST_SHUTDOWN_FLAG_ABORT*
    );

void
MSH3_CALL
MsH3RequestClose(
    MSH3_REQUEST* Request
    );

//
// Listener Interface
//

typedef enum MSH3_LISTENER_EVENT_TYPE {
    MSH3_LISTENER_EVENT_SHUTDOWN_COMPLETE                 = 0,    // Ready for the handle to be closed.
    MSH3_LISTENER_EVENT_NEW_CONNECTION                    = 1,
    // Future events may be added. Existing code should
    // return NOT_SUPPORTED for any unknown event.
} MSH3_LISTENER_EVENT_TYPE;

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

typedef
MSH3_STATUS
(MSH3_CALL MSH3_LISTENER_CALLBACK)(
    MSH3_LISTENER* Connection,
    void* Context,
    MSH3_LISTENER_EVENT* Event
    );

typedef MSH3_LISTENER_CALLBACK *MSH3_LISTENER_CALLBACK_HANDLER;

MSH3_LISTENER*
MSH3_CALL
MsH3ListenerOpen(
    MSH3_API* Api,
    const MSH3_ADDR* Address,
    const MSH3_LISTENER_CALLBACK_HANDLER Handler,
    void* Context
    );

void
MSH3_CALL
MsH3ListenerClose(
    MSH3_LISTENER* Listener
    );

#if defined(__cplusplus)
}
#endif

#endif // _MSH3_
