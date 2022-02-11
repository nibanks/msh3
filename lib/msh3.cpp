/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include <msquic.hpp>
#include "msh3.h"

extern "C"
MSH3_HANDLE
MSH3_API
MsH3Open(
    void
    )
{
    return nullptr;
}

extern "C"
void
MSH3_API
MsH3Close(
    MSH3_HANDLE* MsH3
    )
{
    MsH3;
}

extern "C"
void
MSH3_API
MsH3Get(
    MSH3_HANDLE* MsH3,
    const char* ServerName,
    const char* Path
    )
{
    MsH3;
    ServerName;
    Path;
}
