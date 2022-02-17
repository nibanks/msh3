/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#ifndef _MSH3_
#define _MSH3_

#include <stdint.h>
#include <stdbool.h>

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
    bool Unsecure
    );

void
MSH3_CALL
MsH3ConnectionClose(
    MSH3_CONNECTION* Handle
    );

typedef struct MSH3_HEADER {
    const char* Name;
    uint32_t NameLength;
    const char* Value;
    uint32_t ValueLength;
} MSH3_HEADER;

typedef struct MSH3_REQUEST_IF {
    void MSH3_CALL (*HeaderReceived)(void* IfContext, const MSH3_HEADER* Header);
    void MSH3_CALL (*DataReceived)(void* IfContext, uint32_t Length, const uint8_t* Data);
} MSH3_REQUEST_IF;

bool
MSH3_CALL
MsH3ConnectionGet(
    MSH3_CONNECTION* Handle,
    const MSH3_REQUEST_IF* Interface,
    void* IfContext,
    const char* ServerName,
    const char* Path
    );

#if defined(__cplusplus)
}
#endif

#endif // _MSH3_
