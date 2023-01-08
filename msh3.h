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
#define MSH3_CALL __cdecl
#define MSH3_STATUS HRESULT
#else
#include <netinet/ip.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#define MSH3_CALL
#define MSH3_STATUS unsigned int
#endif

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct MSH3_API MSH3_API;
typedef struct MSH3_CONNECTION_IF MSH3_CONNECTION_IF;
typedef struct MSH3_CONNECTION MSH3_CONNECTION;
typedef struct MSH3_REQUEST MSH3_REQUEST;
typedef struct MSH3_CERTIFICATE_CONFIG MSH3_CERTIFICATE_CONFIG;
typedef struct MSH3_CERTIFICATE MSH3_CERTIFICATE;
typedef struct MSH3_LISTENER_IF MSH3_LISTENER_IF;
typedef struct MSH3_LISTENER MSH3_LISTENER;

typedef enum MSH3_REQUEST_FLAGS {
    MSH3_REQUEST_FLAG_NONE              = 0x0000,
    MSH3_REQUEST_FLAG_ALLOW_0_RTT       = 0x0001,   // Allows the use of encrypting with 0-RTT key.
    MSH3_REQUEST_FLAG_FIN               = 0x0004,   // Indicates the request should be gracefully shutdown too.
    MSH3_REQUEST_FLAG_DELAY_SEND        = 0x0008,   // Indicates the send should be delayed because more will be queued soon.
} MSH3_REQUEST_FLAGS;

typedef enum MSH3_REQUEST_SHUTDOWN_FLAGS {
    MSH3_REQUEST_SHUTDOWN_FLAG_NONE          = 0x0000,
    MSH3_REQUEST_SHUTDOWN_FLAG_GRACEFUL      = 0x0001,   // Cleanly closes the send path.
    MSH3_REQUEST_SHUTDOWN_FLAG_ABORT_SEND    = 0x0002,   // Abruptly closes the send path.
    MSH3_REQUEST_SHUTDOWN_FLAG_ABORT_RECEIVE = 0x0004,   // Abruptly closes the receive path.
    MSH3_REQUEST_SHUTDOWN_FLAG_ABORT         = 0x0006,   // Abruptly closes both send and receive paths.
} MSH3_REQUEST_SHUTDOWN_FLAGS;

#ifdef MSH3_SERVER_SUPPORT
typedef enum MSH3_CERTIFICATE_TYPE {
    MSH3_CERTIFICATE_TYPE_NONE,
    MSH3_CERTIFICATE_TYPE_HASH,
    MSH3_CERTIFICATE_TYPE_HASH_STORE,
    MSH3_CERTIFICATE_TYPE_CONTEXT,
    MSH3_CERTIFICATE_TYPE_FILE,
    MSH3_CERTIFICATE_TYPE_FILE_PROTECTED,
    MSH3_CERTIFICATE_TYPE_PKCS12,
#ifdef MSH3_TEST_MODE
    MSH3_CERTIFICATE_TYPE_SELF_SIGNED,
#endif // MSH3_TEST_MODE
} MSH3_CERTIFICATE_TYPE;

typedef enum MSH3_CERTIFICATE_HASH_STORE_FLAGS {
    MSH3_CERTIFICATE_HASH_STORE_FLAG_NONE           = 0x0000,
    MSH3_CERTIFICATE_HASH_STORE_FLAG_MACHINE_STORE  = 0x0001,
} MSH3_CERTIFICATE_HASH_STORE_FLAGS;
#endif // MSH3_SERVER_SUPPORT

typedef union MSH3_ADDR {
    struct sockaddr Ip;
    struct sockaddr_in Ipv4;
    struct sockaddr_in6 Ipv6;
} MSH3_ADDR;

#if _WIN32
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

#ifdef MSH3_SERVER_SUPPORT
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

typedef struct MSH3_CERTIFICATE_CONFIG {
    MSH3_CERTIFICATE_TYPE Type;
    union {
        MSH3_CERTIFICATE_HASH* CertificateHash;
        MSH3_CERTIFICATE_HASH_STORE* CertificateHashStore;
        MSH3_CERTIFICATE_CONTEXT* CertificateContext;
        MSH3_CERTIFICATE_FILE* CertificateFile;
        MSH3_CERTIFICATE_FILE_PROTECTED* CertificateFileProtected;
        MSH3_CERTIFICATE_PKCS12* CertificatePkcs12;
    };
} MSH3_CERTIFICATE_CONFIG;
#endif // MSH3_SERVER_SUPPORT

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

void
MSH3_CALL
MsH3ApiClose(
    MSH3_API* Handle
    );

typedef struct MSH3_CONNECTION_IF {
    void (MSH3_CALL *Connected)(MSH3_CONNECTION* Connection, void* IfContext);
    void (MSH3_CALL *ShutdownByPeer)(MSH3_CONNECTION* Connection, void* IfContext, uint64_t ErrorCode);
    void (MSH3_CALL *ShutdownByTransport)(MSH3_CONNECTION* Connection, void* IfContext, MSH3_STATUS Status);
    void (MSH3_CALL *ShutdownComplete)(MSH3_CONNECTION* Connection, void* IfContext);
    void (MSH3_CALL *NewRequest)(MSH3_CONNECTION* Connection, void* IfContext, MSH3_REQUEST* Request);
} MSH3_CONNECTION_IF;

MSH3_CONNECTION*
MSH3_CALL
MsH3ConnectionOpen(
    MSH3_API* Handle,
    const MSH3_CONNECTION_IF* Interface,
    void* IfContext,
    const char* ServerName,
    const MSH3_ADDR* ServerAddress,
    bool Unsecure
    );

void
MSH3_CALL
MsH3ConnectionClose(
    MSH3_CONNECTION* Handle
    );

#ifdef MSH3_SERVER_SUPPORT
void
MSH3_CALL
MsH3ConnectionSetCallbackInterface(
    MSH3_CONNECTION* Handle,
    const MSH3_CONNECTION_IF* Interface,
    void* IfContext
    );

void
MSH3_CALL
MsH3ConnectionSetCertificate(
    MSH3_CONNECTION* Handle,
    MSH3_CERTIFICATE* Certificate
    );
#endif // MSH3_SERVER_SUPPORT

typedef struct MSH3_REQUEST_IF {
    void (MSH3_CALL *HeaderReceived)(MSH3_REQUEST* Request, void* IfContext, const MSH3_HEADER* Header);
    bool (MSH3_CALL *DataReceived)(MSH3_REQUEST* Request, void* IfContext, uint32_t* Length, const uint8_t* Data);
    void (MSH3_CALL *Complete)(MSH3_REQUEST* Request, void* IfContext, bool Aborted, uint64_t AbortError);
    void (MSH3_CALL *ShutdownComplete)(MSH3_REQUEST* Request, void* IfContext);
    void (MSH3_CALL *DataSent)(MSH3_REQUEST* Request, void* IfContext, void* SendContext);
} MSH3_REQUEST_IF;

MSH3_REQUEST*
MSH3_CALL
MsH3RequestOpen(
    MSH3_CONNECTION* Handle,
    const MSH3_REQUEST_IF* Interface,
    void* IfContext,
    const MSH3_HEADER* Headers,
    size_t HeadersCount,
    MSH3_REQUEST_FLAGS Flags    // Pass MSH3_REQUEST_FLAG_FIN if there is no data to send
    );

void
MSH3_CALL
MsH3RequestClose(
    MSH3_REQUEST* Handle
    );

void
MSH3_CALL
MsH3RequestCompleteReceive(
    MSH3_REQUEST* Handle,
    uint32_t Length
    );

void
MSH3_CALL
MsH3RequestSetReceiveEnabled(
    MSH3_REQUEST* Handle,
    bool Enabled
    );

bool
MSH3_CALL
MsH3RequestSend(
    MSH3_REQUEST* Handle,
    MSH3_REQUEST_FLAGS Flags,
    const void* Data,
    uint32_t DataLength,
    void* AppContext
    );

void
MSH3_CALL
MsH3RequestShutdown(
    MSH3_REQUEST* Handle,
    MSH3_REQUEST_SHUTDOWN_FLAGS Flags,
    uint64_t AbortError // Only for MSH3_REQUEST_SHUTDOWN_FLAG_ABORT*
    );

#ifdef MSH3_SERVER_SUPPORT
void
MSH3_CALL
MsH3RequestSetCallbackInterface(
    MSH3_REQUEST* Handle,
    const MSH3_REQUEST_IF* Interface,
    void* IfContext
    );

bool
MSH3_CALL
MsH3RequestSendHeaders(
    MSH3_REQUEST* Handle,
    const MSH3_HEADER* Headers,
    size_t HeadersCount,
    MSH3_REQUEST_FLAGS Flags
    );

MSH3_CERTIFICATE*
MSH3_CALL
MsH3CertificateOpen(
    MSH3_API* Handle,
    const MSH3_CERTIFICATE_CONFIG* Config
    );

void
MSH3_CALL
MsH3CertificateClose(
    MSH3_CERTIFICATE* Handle
    );

typedef struct MSH3_LISTENER_IF {
    void (MSH3_CALL *NewConnection)(MSH3_LISTENER* Listener, void* IfContext, MSH3_CONNECTION* Connection, const char* ServerName, uint16_t ServerNameLength);
} MSH3_LISTENER_IF;

MSH3_LISTENER*
MSH3_CALL
MsH3ListenerOpen(
    MSH3_API* Handle,
    const MSH3_ADDR* Address,
    const MSH3_LISTENER_IF* Interface,
    void* IfContext
    );

void
MSH3_CALL
MsH3ListenerClose(
    MSH3_LISTENER* Handle
    );
#endif // MSH3_SERVER_SUPPORT

#if defined(__cplusplus)
}
#endif

#endif // _MSH3_
