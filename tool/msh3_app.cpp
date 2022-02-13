/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include "../msh3.h"
#include <stdio.h>
#include <string.h>

int
#if _WIN32
__cdecl
#endif
main(int argc, char **argv)
{
    if (argc < 2 ||
        (
            !strcmp(argv[1], "?") ||
            !strcmp(argv[1], "-?") ||
            !strcmp(argv[1], "--?") ||
            !strcmp(argv[1], "/?") ||
            !strcmp(argv[1], "help")
        )) {
        printf("Usage: msh3 <server> <path>\n");
        return 1;
    }

    if (MsH3Open()) {
        MsH3Get(argv[1], argv[2], false);
        MsH3Close();
    }

    return 0;
}
