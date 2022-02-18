/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include "msh3.h"
#include <stdio.h>
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

void MSH3_CALL Complete(void* , bool Aborted, uint64_t AbortError) {
    if (Print) printf("\n");
    if (Aborted) printf("Request aborted: 0x%lx\n", AbortError);
}

const MSH3_REQUEST_IF Callbacks = { HeaderReceived, DataReceived, Complete };

int MSH3_CALL main(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "?") || !strcmp(argv[1], "help"))) {
        printf("Usage: msh3 [server] [path] [unsecure]\n");
        return 1;
    }

    const char* Host = "www.google.com";
    const char* Path = "/";
    bool Secure = true;

    if (argc > 1) Host = argv[1];
    if (argc > 2) Path = argv[2];
    if (argc > 3 && !strcmp(argv[3], "unsecure")) Secure = false;

    printf("HTTP/3 GET https://%s%s\n\n", Host, Path);

    auto Api = MsH3ApiOpen();
    if (Api) {
        auto Connection = MsH3ConnectionOpen(Api, Host, Secure);
        if (Connection) {
            MsH3ConnectionGet(Connection, &Callbacks, NULL, Host, Path);
            MsH3ConnectionClose(Connection);
        }
        MsH3ApiClose(Api);
    }

    return 0;
}
