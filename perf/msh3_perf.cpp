/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#define MSH3_TEST_MODE 1 // For self-signed certificates on server
#include "msh3.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

using namespace std;

// Mini PAL
#ifdef _WIN32
#define byteswap_uint64 _byteswap_uint64
#else
#define byteswap_uint64 __builtin_bswap64
#endif

struct Arguments {
    bool IsServer() const { return Target == nullptr; }
    const char* Target { nullptr };
    MsH3Addr Address { 4433 };
    uint32_t ConnectionCount { 1 };
    uint32_t RequestCount { 0 };
    uint64_t Upload { 0 };
    uint64_t Download { 0 };
    bool TimedTransfer { false };
    bool RepeatConnection { false };
    bool RepeatRequest { false };
    bool PrintConnection { false };
    bool PrintStream { false };
    bool PrintThroughput { false };
    bool PrintRate { false };
    bool PrintLatency { false };
    uint64_t Time { 0 };
} Args;

void RunServer();
void RunClient();

// Set by RunServer or RunClient.
MSH3_HEADER* Headers = nullptr;
size_t HeadersCount = 0;

#define PERF_DEFAULT_IO_SIZE 0x10000
uint8_t ResponseBuffer[PERF_DEFAULT_IO_SIZE];

const char* TimeUnits[] = { "m", "ms", "us", "s" };
const uint64_t TimeMult[] = { 60 * 1000 * 1000, 1000, 1, 1000 * 1000 };
const char* SizeUnits[] = { "gb", "mb", "kb", "b" };
const uint64_t SizeMult[] = { 1000 * 1000 * 1000, 1000 * 1000, 1000, 1 };

void ParseValueWithUnit(_Null_terminated_ char* value, _Out_ uint64_t* pValue, _Out_ bool* isTimed) {
    *isTimed = false; // Default

    // Search to see if the value has a time unit specified at the end.
    for (uint32_t i = 0; i < ARRAYSIZE(TimeUnits); ++i) {
        size_t len = strlen(TimeUnits[i]);
        if (len < strlen(value) &&
            _strnicmp(value + strlen(value) - len, TimeUnits[i], len) == 0) {
            *isTimed = true;
            value[strlen(value) - len] = '\0';
            *pValue = (uint64_t)atoi(value) * TimeMult[i];
            return;
        }
    }

    // Search to see if the value has a size unit specified at the end.
    for (uint32_t i = 0; i < ARRAYSIZE(SizeUnits); ++i) {
        size_t len = strlen(SizeUnits[i]);
        if (len < strlen(value) &&
            _strnicmp(value + strlen(value) - len, SizeUnits[i], len) == 0) {
            value[strlen(value) - len] = '\0';
            *pValue = (uint64_t)atoi(value) * SizeMult[i];
            return;
        }
    }

    // Default to bytes if no unit is specified.
    *pValue = (uint64_t)atoi(value);
}

void ParseArgs(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "-?") || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        printf(
            "%s usage\n"
            "\n"
            "Server: %s [options]\n"
            "\n"
            " -b, --bind <addr>        A local IP address to bind to (def: *:4433).\n"
            "\n"
            "Client: %s <hostname/ip>[:port] [options]\n"
            "\n"
            " -c, --conns <#>          The number of connections to use. (def:1)\n"
            " -r, --requests <#>       The number of requests to send on at a time. (def:0)\n"
            " -u, --upload <#>[unit]   The length of bytes to send on each request, with an optional (time or length) unit. (def:0)\n"
            " -d, --download <#>[unit] The length of bytes to receive on each request, with an optional (time or length) unit. (def:0)\n"
            " -C, --rconn              Repeat the scenario at the connection level.\n"
            " -R, --rrequest           Repeat the scenario at the request level.\n"
            " -t, --time <#>[unit]     The total runtime, with an optional unit (def unit is us). Only relevant for repeat scenarios. (def:0)\n"
            "   , --pconn              Print connection statistics.\n"
            "   , --pstream            Print stream statistics.\n"
            "   , --ptput              Print throughput information.\n"
            "   , --prate              Print IO rate information.\n"
            "   , --platency           Print latency information.\n"
            "\n",
            argv[0], argv[0], argv[0]);
        exit(-1);
    }

    int i = 1;
    // Check for a hostname or IP address as the first arg (which means it's a client).
    if (argc > 1 && argv[1][0] != '-') {
        Args.Target = argv[i++];
        char *port = strrchr(argv[1], ':'); // Check for a port number.
        if (port) {
            *port = 0; port++;
            Args.Address.SetPort((uint16_t)atoi(port));
        }
    }

    // Parse the rest of the options.
    for (; i < argc; ++i) {
        if /*if (!strcmp(argv[i], "--bind") || !strcmp(argv[i], "-b")) {
            if (++i >= argc) { printf("Missing bind value\n"); exit(-1); }
            Args.Address.Set(argv[i]);

        } else if*/ (!strcmp(argv[i], "--conns") || !strcmp(argv[i], "-c")) {
            if (++i >= argc) { printf("Missing conns value(s)\n"); exit(-1); }
            Args.ConnectionCount = (uint32_t)atoi(argv[i]);

        } else if (!strcmp(argv[i], "--requests") || !strcmp(argv[i], "-r")) {
            if (++i >= argc) { printf("Missing requests value(s)\n"); exit(-1); }
            Args.RequestCount = (uint32_t)atoi(argv[i]);

        } else if (!strcmp(argv[i], "--upload") || !strcmp(argv[i], "-u")) {
            if (++i >= argc) { printf("Missing upload value(s)\n"); exit(-1); }
            ParseValueWithUnit(argv[i], &Args.Upload, &Args.TimedTransfer);

        } else if (!strcmp(argv[i], "--download") || !strcmp(argv[i], "-d")) {
            if (++i >= argc) { printf("Missing download value(s)\n"); exit(-1); }
            ParseValueWithUnit(argv[i], &Args.Download, &Args.TimedTransfer);

        } else if (!strcmp(argv[i], "--rconn") || !strcmp(argv[i], "-C")) {
            Args.RepeatConnection = true;

        } else if (!strcmp(argv[i], "--rrequest") || !strcmp(argv[i], "-R")) {
            Args.RepeatRequest = true;

        } else if (!strcmp(argv[i], "--time") || !strcmp(argv[i], "-t")) {
            if (++i >= argc) { printf("Missing time value\n"); exit(-1); }
            bool isTimed;
            ParseValueWithUnit(argv[i], &Args.Time, &isTimed);
        } else if (!strcmp(argv[i], "--pconn")) {
            Args.PrintConnection = true;
        } else if (!strcmp(argv[i], "--pstream")) {
            Args.PrintStream = true;
        } else if (!strcmp(argv[i], "--ptput")) {
            Args.PrintThroughput = true;
        } else if (!strcmp(argv[i], "--prate")) {
            Args.PrintRate = true;
        } else if (!strcmp(argv[i], "--platency")) {
            Args.PrintLatency = true;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            exit(-1);
        }
    }

    if (Args.RepeatConnection || Args.RepeatRequest) {
        if (Args.Time == 0) {
            printf("A time value must be specified for repeat scenarios\n");
            exit(-1);
        }
    }

    if ((Args.Upload || Args.Download) && !Args.RequestCount) {
        Args.RequestCount = 1; // Just up/down args imply they want a request
    }

    if (Args.RepeatRequest && !Args.RequestCount) {
        printf("Must specify a '--requests' if using '--rrequest'!\n");
        exit(-1);
    }
}

int MSH3_CALL main(int argc, char **argv) {
    ParseArgs(argc, argv);
    if (Args.IsServer()) {
        RunServer();
    } else {
        RunClient();
    }
}

//
// Server
//

struct ServerRequest {
    ServerRequest() { }
    bool ResponseSizeSet{false};
    bool SendShutdown{false};
    bool RecvShutdown{false};
    uint64_t IdealSendBuffer{0x20000};
    uint64_t ResponseSize{0};
    uint64_t BytesSent{0};
    uint64_t OutstandingBytes{0};
};

void SendResponse(ServerRequest* PerfRequest, MsH3Request* Request) {
    while (PerfRequest->BytesSent < PerfRequest->ResponseSize &&
           PerfRequest->OutstandingBytes < PerfRequest->IdealSendBuffer) {

        const uint64_t BytesLeftToSend = PerfRequest->ResponseSize - PerfRequest->BytesSent;
        uint32_t Length = PERF_DEFAULT_IO_SIZE;
        MSH3_REQUEST_SEND_FLAGS Flags = MSH3_REQUEST_SEND_FLAG_NONE;
        if ((uint64_t)Length >= BytesLeftToSend) {
            Length = (uint32_t)BytesLeftToSend;
            Flags = MSH3_REQUEST_SEND_FLAG_FIN;
        }

        bool SendHeaders = PerfRequest->BytesSent == 0;
        PerfRequest->BytesSent += Length;
        PerfRequest->OutstandingBytes += Length;

        if (SendHeaders) {
            Request->Send(Headers, HeadersCount, ResponseBuffer, Length, Flags, (void*)(size_t)Length);
        } else {
            Request->Send(nullptr, 0, ResponseBuffer, Length, Flags, (void*)(size_t)Length);
        }
    }
}

MSH3_STATUS
ServerRequestCallback(
    MsH3Request* Request,
    void* Context,
    MSH3_REQUEST_EVENT* Event
    )
{
    auto PerfRequest = (ServerRequest*)Context;
    switch (Event->Type) {
    case MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE:
        delete PerfRequest;
        break;
    case MSH3_REQUEST_EVENT_DATA_RECEIVED:
        if (!PerfRequest->ResponseSizeSet) {
            memcpy(&PerfRequest->ResponseSize, Event->DATA_RECEIVED.Data, sizeof(uint64_t)); // TODO - Check length
            PerfRequest->ResponseSize = byteswap_uint64(PerfRequest->ResponseSize);
            PerfRequest->ResponseSizeSet = true;
        }
        break;
    case MSH3_REQUEST_EVENT_PEER_SEND_SHUTDOWN:
        if (!PerfRequest->ResponseSizeSet) {
            Request->Shutdown(MSH3_REQUEST_SHUTDOWN_FLAG_ABORT_SEND);
        } else if (PerfRequest->ResponseSize != 0) {
            SendResponse(PerfRequest, Request);
        } else {
            Request->Shutdown(MSH3_REQUEST_SHUTDOWN_FLAG_GRACEFUL);
        }
        break;
    case MSH3_REQUEST_EVENT_PEER_SEND_ABORTED:
    case MSH3_REQUEST_EVENT_PEER_RECEIVE_ABORTED:
        Request->Shutdown(MSH3_REQUEST_SHUTDOWN_FLAG_ABORT_SEND);
        break;
    case MSH3_REQUEST_EVENT_IDEAL_SEND_SIZE:
        if (PerfRequest->IdealSendBuffer < Event->IDEAL_SEND_SIZE.ByteCount) {
            PerfRequest->IdealSendBuffer = Event->IDEAL_SEND_SIZE.ByteCount;
            SendResponse(PerfRequest, Request);
        }
        break;
    case MSH3_REQUEST_EVENT_SEND_COMPLETE:
        PerfRequest->OutstandingBytes -= (uint32_t)(size_t)Event->SEND_COMPLETE.ClientContext;
        if (!Event->SEND_COMPLETE.Canceled) {
            SendResponse(PerfRequest, Request);
        }
        break;
    default:
        break;
    }
    return MSH3_STATUS_SUCCESS;
}

MSH3_STATUS
ServerConnectionCallback(
    MsH3Connection* /* Connection */,
    void*,
    MSH3_CONNECTION_EVENT* Event
    )
{
    switch (Event->Type) {
    case MSH3_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        /*if (!Event->SHUTDOWN_COMPLETE.AppCloseInProgress && Args.PrintStats) {
            PrintConnectionStatistics(Connection);
        }*/
        break;
    case MSH3_CONNECTION_EVENT_NEW_REQUEST:
        // TODO - Combine and pool alloc
        new MsH3Request(Event->NEW_REQUEST.Request, CleanUpAutoDelete, ServerRequestCallback, new ServerRequest());
        break;
    default:
        break;
    }
    return MSH3_STATUS_SUCCESS;
}

void RunServer() {
    MSH3_HEADER ServerHeaders[] = {
        { ":status", 7, "200", 3 },
    };
    Headers = ServerHeaders;
    HeadersCount = sizeof(Headers)/sizeof(MSH3_HEADER);

    MsH3Api Api; if (!Api.IsValid()) exit(-1);
    MSH3_SETTINGS Settings = { 0 };
    Settings.PeerRequestCount = 1000; Settings.IsSet.PeerRequestCount = true;
    MsH3Configuration Configuration(Api, &Settings); if (!Configuration.IsValid()) exit(-1);
    if (MSH3_FAILED(Configuration.LoadConfiguration())) exit(-1);
    MsH3AutoAcceptListener Listener(Api, Args.Address, ServerConnectionCallback);
    if (!Listener.IsValid()) exit(-1);

    printf("Press any key to exit\n");
    getchar();
}

//
// Client
//

struct PerfClientWorker {
    uint64_t ConnectionsQueued {0};
    uint64_t ConnectionsCreated {0};
    uint64_t ConnectionsConnected {0};
    uint64_t ConnectionsActive {0};
    uint64_t ConnectionsCompleted {0};
    uint64_t RequestsStarted {0};
    uint64_t RequestsCompleted {0};
    thread Thread;
    mutex WakeMutex;
    condition_variable WakeEvent;
    PerfClientWorker() { }
    void Start() { Thread = thread(&PerfClientWorker::Run, this); }
    void Wait() { Thread.join(); }
    void Run();
    void QueueNewConnection() {
        InterlockedIncrement64((int64_t*)&ConnectionsQueued);
        lock_guard Lock{WakeMutex};
        WakeEvent.notify_all();
    }
    void StartNewConnection();
    void OnConnectionComplete();
};

struct PerfClient {
    bool IsRunning {true};
    const uint32_t WorkerCount {thread::hardware_concurrency()};
    vector<PerfClientWorker> Workers {WorkerCount};
    mutex CompleteMutex;
    condition_variable CompleteEvent;
    MsH3Api Api;
    MsH3Configuration Configuration {Api};
    PerfClient() {
        if (!Api.IsValid() || !Configuration.IsValid()) exit(-1);
        if (MSH3_FAILED(Configuration.LoadConfiguration({MSH3_CREDENTIAL_TYPE_NONE, MSH3_CREDENTIAL_FLAG_CLIENT, 0}))) exit(-1);
    }
    void Start() {
        // Create and start workers
        for (uint32_t i = 0; i < WorkerCount; ++i) {
            // Calculate how many connections this worker will be responsible for.
            Workers[i].ConnectionsQueued = Args.ConnectionCount / WorkerCount;
            if (Args.ConnectionCount % WorkerCount > i) {
                Workers[i].ConnectionsQueued++;
            }

            Workers[i].Start(); // TODO - Set ideal processor
        }
    }
    void OnConnectionsComplete() { // Called when a worker has completed its set of connections
        if (GetConnectionsCompleted() == (uint64_t)Args.ConnectionCount) {
            lock_guard Lock{CompleteMutex};
            CompleteEvent.notify_all();
        }
    }
    void WaitOnComplete() {
        unique_lock Lock{CompleteMutex};
        CompleteEvent.wait(Lock);
    }
    uint64_t GetConnectionsCompleted() const {
        uint64_t ConnectionsComplete = 0;
        for (uint64_t i = 0; i < WorkerCount; ++i) {
            ConnectionsComplete += Workers[i].ConnectionsCompleted;
        }
        return ConnectionsComplete;
    }
    uint64_t GetConnectedConnections() const {
        uint64_t ConnectedConnections = 0;
        for (uint64_t i = 0; i < WorkerCount; ++i) {
            ConnectedConnections += Workers[i].ConnectionsConnected;
        }
        return ConnectedConnections;
    }
} *Client;

struct PerfClientConnection {
    struct PerfClientWorker& Worker;
    MsH3Connection* Connection {nullptr};
    PerfClientConnection(PerfClientWorker* Worker, const MsH3Configuration& Configuration)
        : Worker(*Worker) {
        Connection = new MsH3Connection(Client->Api);
        if (!Connection->IsValid()) {
            Worker->OnConnectionComplete();
            return;
        }
        if (MSH3_FAILED(Connection->Start(Configuration, Args.Target, Args.Address))) {
            Worker->OnConnectionComplete();
            return;
        }
        InterlockedIncrement64((int64_t*)&Worker->ConnectionsConnected);
    }
};

void PerfClientWorker::Run() {
    while (Client->IsRunning) {
        while (Client->IsRunning && ConnectionsCreated < ConnectionsQueued) {
            StartNewConnection();
        }
        unique_lock Lock{WakeMutex};
        WakeEvent.wait(Lock);
    }
}

void PerfClientWorker::StartNewConnection() {
    InterlockedIncrement64((int64_t*)&ConnectionsCreated);
    InterlockedIncrement64((int64_t*)&ConnectionsActive);
    new PerfClientConnection(this, Client->Configuration);
}

void PerfClientWorker::OnConnectionComplete() {
    InterlockedIncrement64((int64_t*)&ConnectionsCompleted);
    InterlockedDecrement64((int64_t*)&ConnectionsActive);
    if (Args.RepeatConnection) {
        QueueNewConnection();
    } else {
        if (!ConnectionsActive && ConnectionsCreated == ConnectionsQueued) {
            Client->OnConnectionsComplete();
        }
    }
}

void RunClient() {
    MSH3_HEADER ClientHeaders[] = {
        { ":method", 7, "GET", 3 },
        { ":path", 5, "/", 1 },
        { ":scheme", 7, "https", 5 },
        { ":authority", 10, Args.Target, strlen(Args.Target) },
        { "user-agent", 10, "msh3perf", 8 },
        { "accept", 6, "*/*", 3 },
    };
    Headers = ClientHeaders;
    HeadersCount = sizeof(Headers)/sizeof(MSH3_HEADER);
    *(uint64_t*)(ResponseBuffer) = Args.TimedTransfer ? byteswap_uint64(Args.Download) : UINT64_MAX;

    // TODO - Pre-resolve target address

    Client = new PerfClient();

    Client->Start();

    Client->WaitOnComplete();

    if (Client->GetConnectedConnections() == 0) {
        printf("Error: No Successful Connections!\n");
        return;
    }

    unsigned long long CompletedConnections = Client->GetConnectionsCompleted();
    unsigned long long CompletedStreams = Client->GetStreamsCompleted();

    if (Args.PrintRate) {
        if (CompletedConnections) {
            unsigned long long HPS = CompletedConnections * 1000 * 1000 / Args.Time;
            printf("Result: %llu HPS\n", HPS);
        }
        if (CompletedStreams) {
            unsigned long long RPS = CompletedStreams * 1000 * 1000 / Args.Time;
            printf("Result: %llu RPS\n", RPS);
        }
    } else if (!Args.PrintThroughput && !Args.PrintLatency) {
        if (CompletedConnections && CompletedStreams) {
            printf(
                "Completed %llu connections and %llu streams!\n",
                CompletedConnections, CompletedStreams);
        } else if (CompletedConnections) {
            printf("Completed %llu connections!\n", CompletedConnections);
        } else if (CompletedStreams) {
            printf("Completed %llu streams!\n", CompletedStreams);
        } else {
            printf("No connections or streams completed!\n");
        }
    }
}
