/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include "msquic/src/inc/msquic.h"
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
    uint32_t RequestsSent = 0;
    uint32_t ResponsesReceived = 0;
    uint64_t TotalTime = 0;
    uint64_t MinTime = UINT64_MAX;
    uint64_t MaxTime = 0;

    void RecordResponse(uint64_t timeMs) {
        ResponsesReceived++;
        TotalTime += timeMs;
        if (timeMs < MinTime) MinTime = timeMs;
        if (timeMs > MaxTime) MaxTime = timeMs;
    }
};

struct PingRequest {
    steady_clock::time_point StartTime;
};

struct Arguments {
    const char* Host { nullptr };
    MsH3Addr Address { 443 };
    QUIC_ADDR_STR AddressStr { 0 };
    const char* Path { "/" };
    MSH3_CREDENTIAL_FLAGS Flags { MSH3_CREDENTIAL_FLAG_CLIENT };
    bool Verbose { false };
    uint32_t Count { 4 };  // Default to 4 pings like traditional ping, 0 = infinite
    uint32_t Interval { 1000 }; // Default 1 second interval
    uint32_t Timeout { 5000 };  // Default 5 second timeout
    bool UseGet { false }; // Default to HEAD, use GET if specified
    std::atomic_int CompletionCount { 0 };
    MsH3Connection* Connection { nullptr };
    PingStats Stats;
} Args;

MSH3_STATUS
MsH3RequestHandler(
    MsH3Request* Request,
    void* Context,
    MSH3_REQUEST_EVENT* Event
    )
{
    auto pingRequest = (PingRequest*)Context;

    switch (Event->Type) {
    case MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE:
        delete pingRequest;
        if (++Args.CompletionCount == (int)Args.Count && Args.Count > 0) {
            Args.Connection->Shutdown();
        }
        break;
    case MSH3_REQUEST_EVENT_PEER_SEND_SHUTDOWN: {
        auto duration = duration_cast<microseconds>(steady_clock::now() - pingRequest->StartTime).count() / 1000.0;
        Args.Stats.RecordResponse((uint64_t)(duration));
        printf("Response from %s: time=%.3fms\n", Args.AddressStr.Address, duration);
        break;
    }
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
            auto Data = Event->DATA_RECEIVED.Data;
            auto Length = Event->DATA_RECEIVED.Length;
            printf("Received payload: ");
            fwrite(Data, 1, Length, stdout);
            printf("\n");
        }
        break;
    case MSH3_REQUEST_EVENT_PEER_SEND_ABORTED:
    case MSH3_REQUEST_EVENT_PEER_RECEIVE_ABORTED:
        Request->Shutdown(MSH3_REQUEST_SHUTDOWN_FLAG_ABORT, 0);
        break;
    default:
        break;
    }

    return MSH3_STATUS_SUCCESS;
}

static
MSH3_STATUS
MsH3ConnectionHandler(
    MsH3Connection* /* Connection */,
    void* /* Context */,
    MSH3_CONNECTION_EVENT* Event
    ) noexcept {
    if (Event->Type == MSH3_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER) {
        printf("Connection shutdown initiated by peer: 0x%llx\n",
               (long long unsigned)Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);

    } else if (Event->Type == MSH3_CONNECTION_EVENT_NEW_REQUEST) {
        //
        // Not great beacuse it doesn't provide an application specific
        // error code. If you expect to get streams, you should not no-op
        // the callbacks.
        //
        MsH3RequestClose(Event->NEW_REQUEST.Request);
    }
    return MSH3_STATUS_SUCCESS;
}

void PrintUsage(const char* progName) {
    printf("h3ping - HTTP/3 connectivity testing tool\n");
    printf("Usage: %s <server[:port]> [options...]\n", progName);
    printf("Options:\n");
    printf("  -c, --count <num>      Number of requests to send (default=4, 0=infinite)\n");
    printf("  -g, --get              Use GET requests instead of HEAD (default=HEAD)\n");
    printf("  -h, --help             Print this help text\n");
    printf("  -i, --interval <ms>    Interval between requests in milliseconds (default=1000)\n");
    printf("  -p, --path <path>      Path to request (default=/)\n");
    printf("  -t, --timeout <ms>     Timeout for each request in milliseconds (default=5000)\n");
    printf("  -u, --unsecure         Allow unsecure connections\n");
    printf("  -v, --verbose          Enable verbose output\n");
    printf("  -V, --version          Print version information\n");
}

void ParseArgs(int argc, char **argv) {
    // Check for version and help first, before requiring server argument
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-?") || !strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            PrintUsage(argv[0]);
            exit(0);
        } else if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-V")) {
            uint32_t Version[4]; MsH3Version(Version);
            printf("h3ping using msh3 v%u.%u.%u.%u\n", Version[0], Version[1], Version[2], Version[3]);
            exit(0);
        }
    }

    if (argc < 2) {
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

        } else if (!strcmp(argv[i], "--get") || !strcmp(argv[i], "-g")) {
            Args.UseGet = true;

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

        } else {
            printf("Unknown option: %s\n", argv[i]);
            PrintUsage(argv[0]);
            exit(-1);
        }
    }
}

bool SendPingRequest(MsH3Connection& Connection) {
    const char* method = Args.UseGet ? "GET" : "HEAD";
    MSH3_HEADER Headers[] = {
        { ":method", 7, method, strlen(method) },
        { ":path", 5, Args.Path, strlen(Args.Path) },
        { ":scheme", 7, "https", 5 },
        { ":authority", 10, Args.Host, strlen(Args.Host) },
        { "user-agent", 10, "h3ping/1.0", 10 },
    };
    const size_t HeadersCount = sizeof(Headers)/sizeof(MSH3_HEADER);

    auto pingRequest = new PingRequest();
    pingRequest->StartTime = steady_clock::now();

    auto Request = new (std::nothrow) MsH3Request(Connection, MSH3_REQUEST_FLAG_NONE, CleanUpAutoDelete, MsH3RequestHandler, pingRequest);
    if (!Request || !Request->IsValid()) {
        printf("Failed to create request\n");
        Request->Shutdown(MSH3_REQUEST_SHUTDOWN_FLAG_ABORT, 0);
        delete pingRequest;
        return false;
    }

    if (!Request->Send(Headers, HeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN)) {
        printf("Failed to send request\n");
        Request->Shutdown(MSH3_REQUEST_SHUTDOWN_FLAG_ABORT, 0);
        delete pingRequest;
        return false;
    }

    return true;
}

int MSH3_CALL main(int argc, char **argv) {
    ParseArgs(argc, argv);

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

    MsH3Connection Connection(Api, CleanUpManual, MsH3ConnectionHandler);
    if (!Connection.IsValid()) {
        printf("Failed to create connection\n");
        return -1;
    }

    Args.Connection = &Connection;

    if (MSH3_FAILED(Connection.Start(Configuration, Args.Host, Args.Address))) {
        printf("Failed to start connection\n");
        return -1;
    }

    uint32_t addressSize = sizeof(Args.Address);
    MsH3ConnectionGetQuicParam(Connection, QUIC_PARAM_CONN_REMOTE_ADDRESS, &addressSize, &Args.Address);
    QuicAddrToString((QUIC_ADDR*)&Args.Address, &Args.AddressStr);

    const char* method = Args.UseGet ? "GET" : "HEAD";
    printf("\nPinging %s [%s] with HTTP/3 %s requests:\n", Args.Host, Args.AddressStr.Address, method);

    // Wait for connection to establish
    if (!Connection.Connected.WaitFor(Args.Timeout)) {
        printf("Connection timeout\n");
        return -1;
    }

    // Send ping requests - unified loop for both infinite and finite modes
    for (uint32_t i = 0; Args.Count == 0 || i < Args.Count; ++i) {
        if (!SendPingRequest(Connection)) break;

        // Wait for interval before next request (except for last request in finite mode)
        if (Args.Interval > 0 && (Args.Count == 0 || i < Args.Count - 1)) {
            this_thread::sleep_for(milliseconds(Args.Interval));
        }
    }

    // Wait for all responses
    Connection.ShutdownComplete.Wait();

    QUIC_STATISTICS_V2 stats = {0};
    uint32_t statsSize = sizeof(stats);
    MsH3ConnectionGetQuicParam(Connection, QUIC_PARAM_CONN_STATISTICS_V2, &statsSize, &stats);
    printf("\nPing statistics for %s:\n", Args.Host);
    printf("  Packets: Sent: %llu, Received: %llu, Lost: %llu (%.1f%% loss)\n",
        (unsigned long long)stats.SendTotalPackets,
        (unsigned long long)stats.RecvTotalPackets,
        (unsigned long long)(stats.SendSuspectedLostPackets),
        (100.0 * stats.SendSuspectedLostPackets / stats.SendTotalPackets));

    printf("Approximate round trip times in milliseconds:\n");
    printf("  Minimum: %llums, Maximum: %llums, Average: %llums\n",
        (unsigned long long)Args.Stats.MinTime,
        (unsigned long long)Args.Stats.MaxTime,
        (unsigned long long)(Args.Stats.TotalTime / Args.Stats.ResponsesReceived));

    return 0;
}
