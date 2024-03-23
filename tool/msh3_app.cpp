/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include "msh3.hpp"
#include <vector>
#include <cstring>
#include <atomic>

using namespace std;

struct Arguments {
    const char* Host { nullptr };
    MsH3Addr Address { 443 };
    vector<const char*> Paths;
    MSH3_CREDENTIAL_FLAGS Flags { MSH3_CREDENTIAL_FLAG_CLIENT };
    bool Print { false };
    uint32_t Count { 1 };
    std::atomic_int CompletionCount { 0 };
    MsH3Connection* Connection { nullptr };
} Args;

MSH3_STATUS
MsH3RequestHandler(
    MsH3Request* Request,
    void* Context,
    MSH3_REQUEST_EVENT* Event
    )
{
    const uint32_t Index = (uint32_t)(size_t)Context;
    switch (Event->Type) {
    case MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE:
        if (Args.Print) printf("\n");
        /*if (Aborted) printf("Request %u aborted: 0x%llx\n", Index, (long long unsigned)AbortError);
        else*/         printf("Request %u complete\n", Index);
        if (++Args.CompletionCount == (int)Args.Count) {
            Args.Connection->Shutdown();
        }
        break;
    case MSH3_REQUEST_EVENT_HEADER_RECEIVED:
        if (Args.Print) {
            auto Header = Event->HEADER_RECEIVED.Header;
            fwrite(Header->Name, 1, Header->NameLength, stdout);
            printf(":");
            fwrite(Header->Value, 1, Header->ValueLength, stdout);
            printf("\n");
        }
        break;
    case MSH3_REQUEST_EVENT_DATA_RECEIVED:
        if (Args.Print) {
            fwrite(Event->DATA_RECEIVED.Data, 1, Event->DATA_RECEIVED.Length, stdout);
        }
        break;
    default:
        break;
    }
    return MSH3_STATUS_SUCCESS;
}

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
        Args.Address.SetPort((uint16_t)atoi(port));
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
            Args.Flags |= MSH3_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;

        } else if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v")) {
            Args.Print = true;

        } else if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-V")) {
            uint32_t Version[4]; MsH3Version(Version);
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

    MsH3Api Api;
    if (Api.IsValid()) {
        MsH3Configuration Configuration(Api); if (!Configuration.IsValid()) exit(-1);
        if (MSH3_FAILED(Configuration.LoadConfiguration({MSH3_CREDENTIAL_TYPE_NONE, Args.Flags, 0}))) exit(-1);
        MsH3Connection Connection(Api); if (!Connection.IsValid()) exit(-1);
        Args.Connection = &Connection;
        if (MSH3_FAILED(Connection.Start(Configuration, Args.Host, Args.Address))) exit(-1);
        for (auto Path : Args.Paths) {
            printf("HTTP/3 GET https://%s%s\n", Args.Host, Path);
            Headers[1].Value = Path;
            Headers[1].ValueLength = strlen(Path);
            for (uint32_t i = 0; i < Args.Count; ++i) {
                auto Request = new (std::nothrow) MsH3Request(Connection, MSH3_REQUEST_FLAG_NONE, CleanUpAutoDelete, MsH3RequestHandler, (void*)(size_t)(i+1));
                if (!Request || !Request->IsValid()) {
                    printf("Request %u failed to start\n", i+1);
                    break;
                }
                Request->Send(Headers, HeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN);
            }
        }
        Connection.ShutdownComplete.Wait();
    }

    return 0;
}
