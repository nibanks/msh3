/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#ifndef _MSH3_
#define _MSH3_

#ifdef _WIN32
#pragma once
#endif

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

bool
MSH3_CALL
MsH3ConnectionGet(
    MSH3_CONNECTION* Handle,
    const char* ServerName,
    const char* Path
    );

#if defined(__cplusplus)
}
#endif

#endif // _MSH3_
