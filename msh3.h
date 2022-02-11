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

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef _WIN32
#define MSH3_API __cdecl
#else
#define MSH3_API
#endif

void
MSH3_API
MsH3Get(
    const char* ServerName,
    const char* Path
    );

#if defined(__cplusplus)
}
#endif

#endif // _MSH3_

