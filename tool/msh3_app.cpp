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

    const char* Host = "outlook-evergreen.office.com";
    const char* Path = "index.html";

    if (argc > 1) Host = argv[1];
    if (argc > 2) Path = argv[1];

    printf("HTTP/3 GET https://%s/%s\n\n", Host, Path);

    if (MsH3Open()) {
        MsH3Get(Host, Path, false);
        MsH3Close();
    }

    return 0;
}
