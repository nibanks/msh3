/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include "msh3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace std;

struct Arguments {
    const char* Host { nullptr };
    uint16_t Port { 443 };
    vector<const char*> Paths;
    bool Unsecure { false };
    bool Print { false };
    uint32_t Count { 1 };
    Arguments() { }
} Args;

void MSH3_CALL HeaderReceived(MSH3_REQUEST* , void* , const MSH3_HEADER* Header) {
    if (Args.Print) {
        fwrite(Header->Name, 1, Header->NameLength, stdout);
        printf(":");
        fwrite(Header->Value, 1, Header->ValueLength, stdout);
        printf("\n");
    }
}

void MSH3_CALL DataReceived(MSH3_REQUEST* , void* , uint32_t Length, const uint8_t* Data) {
    if (Args.Print) fwrite(Data, 1, Length, stdout);
}

void MSH3_CALL Complete(MSH3_REQUEST* , void* Context, bool Aborted, uint64_t AbortError) {
    const uint32_t Index = (uint32_t)(size_t)Context;
    if (Args.Print) printf("\n");
    if (Aborted) printf("Request %u aborted: 0x%llx\n", Index, (long long unsigned)AbortError);
    else         printf("Request %u complete\n", Index);
}

void MSH3_CALL Shutdown(MSH3_REQUEST* Request, void* ) {
    MsH3RequestClose(Request);
}

const MSH3_REQUEST_IF Callbacks = { HeaderReceived, DataReceived, Complete, Shutdown };

void ParseArgs(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "-?") || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        printf("usage: %s <server[:port]> [options...]\n"
               " -c, --count <num>      The number of times to query each path (def=1)\n"
               " -h, --help             Prints this help text\n"
               " -p, --path <path(s)>   The paths to query\n"
               " -u, --unsecure         Allows unsecure connections\n"
               " -v, --verbose          Enables verbose output\n"
               " -V, --version          Prints out the version\n",
              argv[0]);
        exit(-1);
    }

    // Parse the server[:port] argument.
    Args.Host = argv[1];
    char *port = strrchr(argv[1], ':');
    if (port) {
        *port = 0; port++;
        Args.Port = (uint16_t)atoi(port);
    }

    // Parse options.
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--count") || !strcmp(argv[i], "-c")) {
            if (++i >= argc) { printf("Missing count value\n"); exit(-1); }
            Args.Count = (uint32_t)atoi(argv[i]);

        } else if (!strcmp(argv[i], "--path") || !strcmp(argv[i], "-p")) {
            if (++i >= argc) { printf("Missing path value(s)\n"); exit(-1); }

            char* Path = (char*)argv[i];
            do {
                char* End = strchr(Path, ',');
                if (End) *End = 0;
                Args.Paths.push_back(Path);
                if (!End) break;
                Path = End + 1;
            } while (true);

        } else if (!strcmp(argv[i], "--unsecure") || !strcmp(argv[i], "-u")) {
            Args.Unsecure = true;

        } else if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v")) {
            Args.Print = true;

        } else if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-V")) {
            uint32_t Version[4];
            MsH3Version(Version);
            printf("Using msh3 v%u.%u.%u.%u\n", Version[0], Version[1], Version[2], Version[3]);
        }
    }

    if (Args.Paths.empty()) {
        Args.Paths.push_back("/");
    }
}

int MSH3_CALL main(int argc, char **argv) {
    ParseArgs(argc, argv);

    MSH3_HEADER Headers[] = {
        { ":method", 7, "GET", 3 },
        { ":path", 5, Args.Paths[0], strlen(Args.Paths[0]) },
        { ":scheme", 7, "https", 5 },
        { ":authority", 10, Args.Host, strlen(Args.Host) },
        { "user-agent", 10, "curl/7.82.0-DEV", 15 },
        { "accept", 6, "*/*", 3 },
    };
    const size_t HeadersCount = sizeof(Headers)/sizeof(MSH3_HEADER);

    auto Api = MsH3ApiOpen();
    if (Api) {
        auto Connection = MsH3ConnectionOpen(Api, Args.Host, Args.Port, Args.Unsecure);
        if (Connection) {
            for (auto Path : Args.Paths) {
                printf("HTTP/3 GET https://%s:%d%s\n\n", Args.Host, Args.Port, Path);
                Headers[1].Value = Path;
                Headers[1].ValueLength = strlen(Path);
                for (uint32_t i = 0; i < Args.Count; ++i) {
                    auto Request = MsH3RequestOpen(Connection, &Callbacks, (void*)(size_t)(i+1), Headers, HeadersCount, nullptr, 0);
                    if (!Request) {
                        printf("Request %u failed to start\n", i+1);
                        break;
                    }
                }
            }
            MsH3ConnectionClose(Connection);
        }
        MsH3ApiClose(Api);
    }

    return 0;
}
