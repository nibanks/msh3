/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include "msh3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool Print = true;

void MSH3_CALL HeaderReceived(void* , const MSH3_HEADER* Header) {
    if (Print) {
        fwrite(Header->Name, 1, Header->NameLength, stdout);
        printf(":");
        fwrite(Header->Value, 1, Header->ValueLength, stdout);
        printf("\n");
    }
}

void MSH3_CALL DataReceived(void* , uint32_t Length, const uint8_t* Data) {
    if (Print) fwrite(Data, 1, Length, stdout);
}

void MSH3_CALL Complete(void* Context, bool Aborted, uint64_t AbortError) {
    const uint32_t Index = (uint32_t)(size_t)Context;
    if (Print) printf("\n");
    if (Aborted) printf("Request %u aborted: 0x%lx\n", Index, AbortError);
    else         printf("Request %u complete\n", Index);
}

const MSH3_REQUEST_IF Callbacks = { HeaderReceived, DataReceived, Complete };

int MSH3_CALL main(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "?") || !strcmp(argv[1], "help"))) {
        printf("Usage: msh3 [server] [path] [unsecure] [count]\n");
        return 1;
    }

    const char* Host = "msquic.net";
    const char* Path = "/";
    bool Secure = true;
    uint32_t Count = 1;

    if (argc > 1) Host = argv[1];
    if (argc > 2) Path = argv[2];
    if (argc > 3 && !strcmp(argv[3], "unsecure")) Secure = false;
    if (argc > 4) Count = (uint32_t)atoi(argv[4]);
    Print = Count == 1;

    printf("HTTP/3 GET https://%s%s\n\n", Host, Path);

    auto Api = MsH3ApiOpen();
    if (Api) {
        auto Connection = MsH3ConnectionOpen(Api, Host, Secure);
        if (Connection) {
            for (uint32_t i = 0; i < Count; ++i)
                if (!MsH3ConnectionGet(Connection, &Callbacks, (void*)(size_t)(i+1), Host, Path)) {
                    printf("Request %u failed to start\n", i+1);
                    break;
                }
            MsH3ConnectionClose(Connection);
        }
        MsH3ApiClose(Api);
    }

    return 0;
}
