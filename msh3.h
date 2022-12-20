/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#ifndef _MSH3_
#define _MSH3_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef _WIN32
#define MSH3_CALL __cdecl
#else
#define MSH3_CALL
#endif

typedef struct MSH3_API MSH3_API;
typedef struct MSH3_CONNECTION MSH3_CONNECTION;
typedef struct MSH3_REQUEST MSH3_REQUEST;

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

MSH3_CONNECTION*
MSH3_CALL
MsH3ConnectionOpen(
    MSH3_API* Handle,
    const char* ServerName,
    uint16_t Port,
    bool Unsecure
    );

void
MSH3_CALL
MsH3ConnectionClose(
    MSH3_CONNECTION* Handle
    );

typedef enum MSH3_CONNECTION_STATE {
    MSH3_CONN_CONNECTING,
    MSH3_CONN_HANDSHAKE_FAILED,
    MSH3_CONN_CONNECTED,
    MSH3_CONN_DISCONNECTED,
} MSH3_CONNECTION_STATE;

MSH3_CONNECTION_STATE
MSH3_CALL
MsH3ConnectionGetState(
    MSH3_CONNECTION* Handle,
    bool WaitForHandshakeComplete
    );

typedef struct MSH3_HEADER {
    const char* Name;
    size_t NameLength;
    const char* Value;
    size_t ValueLength;
} MSH3_HEADER;

typedef struct MSH3_REQUEST_IF {
    void (MSH3_CALL *HeaderReceived)(MSH3_REQUEST* Request, void* IfContext, const MSH3_HEADER* Header);
    void (MSH3_CALL *DataReceived)(MSH3_REQUEST* Request, void* IfContext, uint32_t Length, const uint8_t* Data);
    void (MSH3_CALL *Complete)(MSH3_REQUEST* Request, void* IfContext, bool Aborted, uint64_t AbortError);
    void (MSH3_CALL *Shutdown)(MSH3_REQUEST* Request, void* IfContext);
    void (MSH3_CALL *DataSent)(MSH3_REQUEST* Request, void* IfContext, void* SendContext);
} MSH3_REQUEST_IF;

typedef enum MSH3_REQUEST_FLAGS {
    MSH3_REQUEST_FLAG_NONE              = 0x0000,
    MSH3_REQUEST_FLAG_ALLOW_0_RTT       = 0x0001,   // Allows the use of encrypting with 0-RTT key.
    MSH3_REQUEST_FLAG_FIN               = 0x0004,   // Indicates the request should be gracefully shutdown too.
    MSH3_REQUEST_FLAG_DELAY_SEND        = 0x0008,   // Indicates the send should be delayed because more will be queued soon.
} MSH3_REQUEST_FLAGS;

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

bool
MSH3_CALL
MsH3RequestSend(
    MSH3_REQUEST* Handle,
    MSH3_REQUEST_FLAGS Flags,
    const void* Data,
    uint32_t DataLength,
    void* AppContext
    );

typedef enum MSH3_REQUEST_SHUTDOWN_FLAGS {
    MSH3_REQUEST_SHUTDOWN_FLAG_NONE          = 0x0000,
    MSH3_REQUEST_SHUTDOWN_FLAG_GRACEFUL      = 0x0001,   // Cleanly closes the send path.
    MSH3_REQUEST_SHUTDOWN_FLAG_ABORT_SEND    = 0x0002,   // Abruptly closes the send path.
    MSH3_REQUEST_SHUTDOWN_FLAG_ABORT_RECEIVE = 0x0004,   // Abruptly closes the receive path.
    MSH3_REQUEST_SHUTDOWN_FLAG_ABORT         = 0x0006,   // Abruptly closes both send and receive paths.
} MSH3_REQUEST_SHUTDOWN_FLAGS;

void
MSH3_CALL
MsH3RequestShutdown(
    MSH3_REQUEST* Handle,
    MSH3_REQUEST_SHUTDOWN_FLAGS Flags,
    uint64_t AbortError // Only for MSH3_REQUEST_SHUTDOWN_FLAG_ABORT*
    );

void
MSH3_CALL
MsH3RequestClose(
    MSH3_REQUEST* Handle
    );

#if defined(__cplusplus)
}
#endif

#endif // _MSH3_
