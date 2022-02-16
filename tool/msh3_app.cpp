/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include "msh3.h"
#include <stdio.h>
#include <string.h>

int
#if _WIN32
__cdecl
#endif
main(int argc, char **argv)
{
    if (argc > 1 &&
        (
            !strcmp(argv[1], "?") ||
            !strcmp(argv[1], "-?") ||
            !strcmp(argv[1], "--?") ||
            !strcmp(argv[1], "/?") ||
            !strcmp(argv[1], "help")
        )) {
        printf("Usage: msh3 [server] [path]\n");
        return 1;
    }

    //const char* Host = "outlook-evergreen.office.com";
    const char* Host = "www.google.com";
    //const char* Host = "www.cloudflare.com";
    const char* Path = "/";
    bool Secure = true;

    if (argc > 1) Host = argv[1];
    if (argc > 2) Path = argv[2];
    if (argc > 3 && strcmp(argv[3], "unsecure")) Secure = false;

    printf("HTTP/3 GET https://%s%s\n\n", Host, Path);

    auto Api = MsH3ApiOpen();
    if (Api) {
        auto Connection = MsH3ConnectionOpen(Api, Host, Secure);
        if (Connection) {
            MsH3ConnectionGet(Connection, Host, Path);
            MsH3ConnectionClose(Connection);
        }
        MsH3ApiClose(Api);
    }

    return 0;
}
