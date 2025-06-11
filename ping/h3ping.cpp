/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include "msh3.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace std;
using namespace std::chrono;

struct PingStats {
    uint32_t PacketsSent = 0;
    uint32_t PacketsReceived = 0;
    uint64_t TotalTime = 0;
    uint64_t MinTime = UINT64_MAX;
    uint64_t MaxTime = 0;
    bool FirstPacket = true;
    
    void RecordResponse(uint64_t timeMs) {
        PacketsReceived++;
        TotalTime += timeMs;
        if (FirstPacket || timeMs < MinTime) MinTime = timeMs;
        if (timeMs > MaxTime) MaxTime = timeMs;
        FirstPacket = false;
    }
    
    void PrintStats(const char* host) {
        printf("\n--- %s HTTP/3 ping statistics ---\n", host);
        printf("%u requests transmitted, %u received, %.1f%% packet loss\n",
               PacketsSent, PacketsReceived, 
               PacketsSent > 0 ? ((PacketsSent - PacketsReceived) * 100.0 / PacketsSent) : 0.0);
        if (PacketsReceived > 0) {
            printf("round-trip min/avg/max = %llu/%llu/%llu ms\n",
                   (unsigned long long)MinTime,
                   (unsigned long long)(TotalTime / PacketsReceived),
                   (unsigned long long)MaxTime);
        }
    }
};

struct PingRequest {
    steady_clock::time_point StartTime;
    uint32_t Index;
};

struct Arguments {
    const char* Host { nullptr };
    MsH3Addr Address { 443 };
    const char* Path { "/" };
    MSH3_CREDENTIAL_FLAGS Flags { MSH3_CREDENTIAL_FLAG_CLIENT };
    bool Verbose { false };
    uint32_t Count { 4 };  // Default to 4 pings like traditional ping
    uint32_t Interval { 1000 }; // Default 1 second interval
    uint32_t Timeout { 5000 };  // Default 5 second timeout
    std::atomic_int CompletionCount { 0 };
    MsH3Connection* Connection { nullptr };
    PingStats Stats;
    bool Infinite { false };
} Args;

MSH3_STATUS
MsH3RequestHandler(
    MsH3Request* /* Request */,
    void* Context,
    MSH3_REQUEST_EVENT* Event
    )
{
    auto pingRequest = (PingRequest*)Context;
    auto endTime = steady_clock::now();
    
    switch (Event->Type) {
    case MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE:
        {
            auto duration = duration_cast<milliseconds>(endTime - pingRequest->StartTime).count();
            Args.Stats.RecordResponse(duration);
            
            printf("Reply from %s: time=%llums\n", Args.Host, (unsigned long long)duration);
            
            if (++Args.CompletionCount == (int)Args.Count && !Args.Infinite) {
                Args.Connection->Shutdown();
            }
        }
        break;
    case MSH3_REQUEST_EVENT_HEADER_RECEIVED:
        if (Args.Verbose) {
            auto Header = Event->HEADER_RECEIVED.Header;
            printf("Header: ");
            fwrite(Header->Name, 1, Header->NameLength, stdout);
            printf(": ");
            fwrite(Header->Value, 1, Header->ValueLength, stdout);
            printf("\n");
        }
        break;
    case MSH3_REQUEST_EVENT_DATA_RECEIVED:
        if (Args.Verbose) {
            printf("Received %u bytes of data\n", Event->DATA_RECEIVED.Length);
        }
        break;
    case MSH3_REQUEST_EVENT_PEER_SEND_SHUTDOWN:
        // Request completed successfully
        break;
    case MSH3_REQUEST_EVENT_PEER_SEND_ABORTED:
        {
            auto duration = duration_cast<milliseconds>(endTime - pingRequest->StartTime).count();
            printf("Request %u aborted: 0x%llx (time=%llums)\n", 
                   pingRequest->Index, (long long unsigned)Event->PEER_SEND_ABORTED.ErrorCode, 
                   (unsigned long long)duration);
            if (++Args.CompletionCount == (int)Args.Count && !Args.Infinite) {
                Args.Connection->Shutdown();
            }
        }
        break;
    default:
        break;
    }
    
    // Clean up the ping request context if the request is complete
    if (Event->Type == MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE || 
        Event->Type == MSH3_REQUEST_EVENT_PEER_SEND_ABORTED) {
        delete pingRequest;
    }
    
    return MSH3_STATUS_SUCCESS;
}

void PrintUsage(const char* progName) {
    printf("h3ping - HTTP/3 connectivity testing tool\n");
    printf("Usage: %s <server[:port]> [options...]\n", progName);
    printf("Options:\n");
    printf("  -c, --count <num>      Number of requests to send (default=4, 0=infinite)\n");
    printf("  -h, --help             Print this help text\n");
    printf("  -i, --interval <ms>    Interval between requests in milliseconds (default=1000)\n");
    printf("  -p, --path <path>      Path to request (default=/)\n");
    printf("  -t, --timeout <ms>     Timeout for each request in milliseconds (default=5000)\n");
    printf("  -u, --unsecure         Allow unsecure connections\n");
    printf("  -v, --verbose          Enable verbose output\n");
    printf("  -V, --version          Print version information\n");
}

void ParseArgs(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "-?") || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        PrintUsage(argv[0]);
        exit(0);
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
            if (Args.Count == 0) Args.Infinite = true;

        } else if (!strcmp(argv[i], "--interval") || !strcmp(argv[i], "-i")) {
            if (++i >= argc) { printf("Missing interval value\n"); exit(-1); }
            Args.Interval = (uint32_t)atoi(argv[i]);

        } else if (!strcmp(argv[i], "--path") || !strcmp(argv[i], "-p")) {
            if (++i >= argc) { printf("Missing path value\n"); exit(-1); }
            Args.Path = argv[i];

        } else if (!strcmp(argv[i], "--timeout") || !strcmp(argv[i], "-t")) {
            if (++i >= argc) { printf("Missing timeout value\n"); exit(-1); }
            Args.Timeout = (uint32_t)atoi(argv[i]);

        } else if (!strcmp(argv[i], "--unsecure") || !strcmp(argv[i], "-u")) {
            Args.Flags |= MSH3_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;

        } else if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v")) {
            Args.Verbose = true;

        } else if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-V")) {
            uint32_t Version[4]; MsH3Version(Version);
            printf("h3ping using msh3 v%u.%u.%u.%u\n", Version[0], Version[1], Version[2], Version[3]);
            exit(0);
            
        } else {
            printf("Unknown option: %s\n", argv[i]);
            PrintUsage(argv[0]);
            exit(-1);
        }
    }
}

bool SendPingRequest(MsH3Connection& Connection) {
    MSH3_HEADER Headers[] = {
        { ":method", 7, "HEAD", 4 },  // Use HEAD for minimal overhead
        { ":path", 5, Args.Path, strlen(Args.Path) },
        { ":scheme", 7, "https", 5 },
        { ":authority", 10, Args.Host, strlen(Args.Host) },
        { "user-agent", 10, "h3ping/1.0", 10 },
    };
    const size_t HeadersCount = sizeof(Headers)/sizeof(MSH3_HEADER);

    auto pingRequest = new PingRequest();
    pingRequest->StartTime = steady_clock::now();
    pingRequest->Index = ++Args.Stats.PacketsSent;
    
    auto Request = new (std::nothrow) MsH3Request(Connection, MSH3_REQUEST_FLAG_NONE, CleanUpAutoDelete, MsH3RequestHandler, pingRequest);
    if (!Request || !Request->IsValid()) {
        printf("Failed to create request %u\n", Args.Stats.PacketsSent);
        delete pingRequest;
        return false;
    }
    
    if (!Request->Send(Headers, HeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN)) {
        printf("Failed to send request %u\n", Args.Stats.PacketsSent);
        delete pingRequest;
        return false;
    }
    
    return true;
}

int MSH3_CALL main(int argc, char **argv) {
    ParseArgs(argc, argv);

    printf("H3PING %s (%s): HTTP/3 connectivity test\n", Args.Host, Args.Host);
    
    MsH3Api Api;
    if (!Api.IsValid()) {
        printf("Failed to initialize MSH3 API\n");
        return -1;
    }
    
    MsH3Configuration Configuration(Api);
    if (!Configuration.IsValid()) {
        printf("Failed to create configuration\n");
        return -1;
    }
    
    if (MSH3_FAILED(Configuration.LoadConfiguration({MSH3_CREDENTIAL_TYPE_NONE, Args.Flags, 0}))) {
        printf("Failed to load configuration\n");
        return -1;
    }
    
    MsH3Connection Connection(Api);
    if (!Connection.IsValid()) {
        printf("Failed to create connection\n");
        return -1;
    }
    
    Args.Connection = &Connection;
    
    printf("Connecting to %s:%u...\n", Args.Host, 443);  // TODO: extract actual port
    if (MSH3_FAILED(Connection.Start(Configuration, Args.Host, Args.Address))) {
        printf("Failed to start connection\n");
        return -1;
    }

    // Wait for connection to establish
    if (!Connection.Connected.WaitFor(Args.Timeout)) {
        printf("Connection timeout\n");
        return -1;
    }
    
    printf("Connected. Starting ping test...\n");

    // Send ping requests
    if (Args.Infinite) {
        printf("Sending infinite requests to %s (press Ctrl+C to stop):\n", Args.Host);
        while (true) {
            if (!SendPingRequest(Connection)) break;
            
            // Wait for interval before next request
            if (Args.Interval > 0) {
                this_thread::sleep_for(milliseconds(Args.Interval));
            }
        }
    } else {
        printf("Sending %u requests to %s:\n", Args.Count, Args.Host);
        for (uint32_t i = 0; i < Args.Count; ++i) {
            if (!SendPingRequest(Connection)) break;
            
            if (i < Args.Count - 1 && Args.Interval > 0) {
                this_thread::sleep_for(milliseconds(Args.Interval));
            }
        }
    }

    // Wait for all responses
    Connection.ShutdownComplete.Wait();
    
    // Print statistics
    Args.Stats.PrintStats(Args.Host);

    return 0;
}