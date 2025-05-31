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
#include <mutex>
#include <thread>
#include <vector>
#include <deque>

// Include Winsock for network functions
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#endif

using namespace std;
using namespace std::chrono;

// Performance metrics
struct RequestMetrics {
    uint64_t StartTime;      // in microseconds
    uint64_t EndTime;        // in microseconds
    uint64_t BytesSent;
    uint64_t BytesReceived;
    bool Success;
};

struct ConnectionMetrics {
    uint64_t StartTime;      // in microseconds
    uint64_t EndTime;        // in microseconds
    std::vector<RequestMetrics> Requests;
    uint32_t SuccessCount;
    uint32_t FailureCount;
    std::mutex Lock;

    // Default constructor
    ConnectionMetrics() = default;

    // Move constructor
    ConnectionMetrics(ConnectionMetrics&& other) noexcept :
        StartTime(other.StartTime),
        EndTime(other.EndTime),
        Requests(std::move(other.Requests)),
        SuccessCount(other.SuccessCount),
        FailureCount(other.FailureCount)
    {}

    // Move assignment
    ConnectionMetrics& operator=(ConnectionMetrics&& other) noexcept {
        if (this != &other) {
            StartTime = other.StartTime;
            EndTime = other.EndTime;
            Requests = std::move(other.Requests);
            SuccessCount = other.SuccessCount;
            FailureCount = other.FailureCount;
        }
        return *this;
    }

    // Delete copy constructor and assignment
    ConnectionMetrics(const ConnectionMetrics&) = delete;
    ConnectionMetrics& operator=(const ConnectionMetrics&) = delete;
};

// Global metrics
std::vector<ConnectionMetrics> Metrics;
std::atomic<uint32_t> TotalRequests {0};
std::atomic<uint32_t> CompletedRequests {0};
std::atomic<uint32_t> SuccessfulRequests {0};
std::atomic<uint32_t> FailedRequests {0};
std::atomic<uint64_t> TotalBytesSent {0};
std::atomic<uint64_t> TotalBytesReceived {0};
std::atomic<bool> Running {true};

// Configuration structure for the performance tool
struct PerfConfig {
    const char* Host { nullptr };
    MsH3Addr Address { 443 };
    bool IsServer { false };
    vector<const char*> Paths;
    MSH3_CREDENTIAL_FLAGS Flags { MSH3_CREDENTIAL_FLAG_CLIENT };
    uint32_t Connections { 1 };
    uint32_t RequestsPerConnection { 10 };
    uint32_t Threads { 1 };
    uint32_t Duration { 10 }; // Seconds
    std::atomic_int CompletionCount { 0 };
    bool Verbose { false };
} Config;

void PrintUsage() {
    printf("\nmsH3 Performance Tool\n\n");
    printf("Usage: msh3perf [client|server] [options]\n\n");
    printf("Client mode options:\n");
    printf("  -h HOST            Target hostname\n");
    printf("  -p PORT            Target port (default: 443)\n");
    printf("  -u PATH            Target path(s) (can specify multiple)\n");
    printf("  -c CONNECTIONS     Number of connections (default: 1)\n");
    printf("  -r REQUESTS        Requests per connection (default: 10)\n");
    printf("  -t THREADS         Number of threads (default: 1)\n");
    printf("  -d DURATION        Test duration in seconds (default: 10)\n");
    printf("  -v                 Verbose output\n\n");
    printf("Server mode options:\n");
    printf("  -p PORT            Listen port (default: 443)\n");
    printf("  -c CONNECTIONS     Max connections (default: 1)\n");
    printf("  -t THREADS         Number of threads (default: 1)\n");
    printf("  -d DURATION        Test duration in seconds (default: 10)\n");
    printf("  -v                 Verbose output\n\n");
}

bool ParseCommandLine(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return false;
    }

    if (strcmp(argv[1], "client") == 0) {
        Config.IsServer = false;
        Config.Flags = MSH3_CREDENTIAL_FLAG_CLIENT;    } else if (strcmp(argv[1], "server") == 0) {
        Config.IsServer = true;
        Config.Flags = MSH3_CREDENTIAL_FLAG_NONE;  // Server mode (no client flag)
    } else {
        PrintUsage();
        return false;
    }

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            if (++i >= argc) {
                printf("Missing hostname\n");
                return false;
            }
            Config.Host = argv[i];
        } else if (strcmp(argv[i], "-p") == 0) {
            if (++i >= argc) {
                printf("Missing port\n");
                return false;
            }
            Config.Address = MsH3Addr((uint16_t)atoi(argv[i]));
        } else if (strcmp(argv[i], "-u") == 0) {
            if (++i >= argc) {
                printf("Missing path\n");
                return false;
            }
            Config.Paths.push_back(argv[i]);
        } else if (strcmp(argv[i], "-c") == 0) {
            if (++i >= argc) {
                printf("Missing connection count\n");
                return false;
            }
            Config.Connections = atoi(argv[i]);
        } else if (strcmp(argv[i], "-r") == 0) {
            if (++i >= argc) {
                printf("Missing request count\n");
                return false;
            }
            Config.RequestsPerConnection = atoi(argv[i]);
        } else if (strcmp(argv[i], "-t") == 0) {
            if (++i >= argc) {
                printf("Missing thread count\n");
                return false;
            }
            Config.Threads = atoi(argv[i]);
        } else if (strcmp(argv[i], "-d") == 0) {
            if (++i >= argc) {
                printf("Missing duration\n");
                return false;
            }
            Config.Duration = atoi(argv[i]);
        } else if (strcmp(argv[i], "-v") == 0) {
            Config.Verbose = true;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            PrintUsage();
            return false;
        }
    }

    // Validate configuration
    if (!Config.IsServer && Config.Host == nullptr) {
        printf("Client mode requires a hostname (-h)\n");
        return false;
    }

    if (!Config.IsServer && Config.Paths.empty()) {
        // Add a default path if none provided
        Config.Paths.push_back("/");
    }

    return true;
}

// Get current timestamp in microseconds
uint64_t GetTimestampUs() {
    return (uint64_t)duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch()).count();
}

// Print performance results
void PrintResults() {
    uint64_t totalDuration = 0;
    uint64_t maxDuration = 0;
    uint64_t minDuration = UINT64_MAX;
    uint64_t sumDurations = 0;
    uint32_t requestCount = 0;

    for (const auto& conn : Metrics) {
        for (const auto& req : conn.Requests) {
            if (req.Success) {
                uint64_t duration = req.EndTime - req.StartTime;
                maxDuration = max(maxDuration, duration);
                minDuration = min(minDuration, duration);
                sumDurations += duration;
                requestCount++;
            }
        }
    }

    printf("\n--- Performance Results ---\n");
    printf("Total Requests: %u\n", TotalRequests.load());
    printf("Completed Requests: %u\n", CompletedRequests.load());
    printf("Successful: %u\n", SuccessfulRequests.load());
    printf("Failed: %u\n", FailedRequests.load());
    printf("Total Bytes Sent: %llu\n", (unsigned long long)TotalBytesSent.load());
    printf("Total Bytes Received: %llu\n", (unsigned long long)TotalBytesReceived.load());

    if (requestCount > 0) {
        double avgLatency = (double)sumDurations / requestCount;
        printf("Min Latency: %.3f ms\n", minDuration / 1000.0);
        printf("Max Latency: %.3f ms\n", maxDuration / 1000.0);
        printf("Avg Latency: %.3f ms\n", avgLatency / 1000.0);
    }
}

// Request callback context
struct RequestContext {
    uint32_t ConnectionIndex;
    uint32_t RequestIndex;
    RequestMetrics Metrics;
};

// Client request handler
MSH3_STATUS
ClientRequestCallback(
    MsH3Request* Request,
    void* Context,
    MSH3_REQUEST_EVENT* Event
    )
{
    auto* RequestCtx = (RequestContext*)Context;

    switch (Event->Type) {
    case MSH3_REQUEST_EVENT_HEADER_RECEIVED:
        if (Config.Verbose) {
            auto Header = Event->HEADER_RECEIVED.Header;
            printf("[C%u:R%u] Header: %.*s: %.*s\n",
                   RequestCtx->ConnectionIndex,
                   RequestCtx->RequestIndex,
                   (int)Header->NameLength, Header->Name,
                   (int)Header->ValueLength, Header->Value);
        }
        break;

    case MSH3_REQUEST_EVENT_DATA_RECEIVED:
        {
            RequestCtx->Metrics.BytesReceived += Event->DATA_RECEIVED.Length;
            TotalBytesReceived += Event->DATA_RECEIVED.Length;

            if (Config.Verbose) {
                printf("[C%u:R%u] Received %u bytes of data\n",
                       RequestCtx->ConnectionIndex,
                       RequestCtx->RequestIndex,
                       Event->DATA_RECEIVED.Length);
            }
              // Acknowledge the data
            MsH3RequestCompleteReceive(Request->Handle, Event->DATA_RECEIVED.Length);
        }
        break;

    case MSH3_REQUEST_EVENT_PEER_SEND_SHUTDOWN:
        if (Config.Verbose) {
            printf("[C%u:R%u] Peer send shutdown\n",
                   RequestCtx->ConnectionIndex,
                   RequestCtx->RequestIndex);
        }
        break;

    case MSH3_REQUEST_EVENT_PEER_SEND_ABORTED:
        if (Config.Verbose) {
            printf("[C%u:R%u] Peer send aborted\n",
                   RequestCtx->ConnectionIndex,
                   RequestCtx->RequestIndex);
        }
        RequestCtx->Metrics.Success = false;
        FailedRequests++;
        break;

    case MSH3_REQUEST_EVENT_PEER_RECEIVE_ABORTED:
        if (Config.Verbose) {
            printf("[C%u:R%u] Peer receive aborted\n",
                   RequestCtx->ConnectionIndex,
                   RequestCtx->RequestIndex);
        }
        break;

    case MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE:
        // Request completed
        RequestCtx->Metrics.EndTime = GetTimestampUs();
        CompletedRequests++;

        if (Config.Verbose) {
            printf("[C%u:R%u] Request completed in %.3f ms\n",
                   RequestCtx->ConnectionIndex,
                   RequestCtx->RequestIndex,
                   (RequestCtx->Metrics.EndTime - RequestCtx->Metrics.StartTime) / 1000.0);
        }

        // Update metrics
        {
            auto& connMetrics = Metrics[RequestCtx->ConnectionIndex];
            std::lock_guard<std::mutex> lock(connMetrics.Lock);
            connMetrics.Requests[RequestCtx->RequestIndex] = RequestCtx->Metrics;

            if (RequestCtx->Metrics.Success) {
                connMetrics.SuccessCount++;
                SuccessfulRequests++;
            } else {
                connMetrics.FailureCount++;
            }
        }

        delete RequestCtx;
        break;
    }

    return MSH3_STATUS_SUCCESS;
}

// Connection callback
MSH3_STATUS
ClientConnectionCallback(
    MsH3Connection* Connection,
    void* Context,
    MSH3_CONNECTION_EVENT* Event
    )
{
    uint32_t connIndex = *(uint32_t*)Context;

    switch (Event->Type) {
    case MSH3_CONNECTION_EVENT_CONNECTED:
        if (Config.Verbose) {
            printf("[C%u] Connected\n", connIndex);
        }
        break;

    case MSH3_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        if (Config.Verbose) {
            printf("[C%u] Shutdown complete\n", connIndex);
        }

        // Update connection metrics
        Metrics[connIndex].EndTime = GetTimestampUs();
        delete (uint32_t*)Context;
        break;
          default:
        if (Config.Verbose) {
            printf("[C%u] Other event: %d\n", connIndex, Event->Type);
        }
        break;
    }

    return MSH3_STATUS_SUCCESS;
}

// Client worker thread function
void ClientWorkerThread(uint32_t threadId, MsH3Api* api) {
    uint32_t connectionsPerThread = Config.Connections / Config.Threads;
    uint32_t startIndex = threadId * connectionsPerThread;
    uint32_t endIndex = startIndex + connectionsPerThread;

    if (Config.Verbose) {
        printf("[T%u] Thread started, handling connections %u to %u\n",
               threadId, startIndex, endIndex - 1);
    }

    for (uint32_t i = startIndex; i < endIndex && Running; i++) {
        // Prepare headers for requests
        MSH3_HEADER headers[] = {
            { ":method", 7, "GET", 3 },
            { ":scheme", 7, "https", 5 },
            { ":path", 5, Config.Paths[i % Config.Paths.size()],
                          (size_t)strlen(Config.Paths[i % Config.Paths.size()]) },
            { ":authority", 10, Config.Host, (size_t)strlen(Config.Host) },
            { "user-agent", 10, "msh3perf/1.0", 11 }
        };        // Create connection
        uint32_t* connContext = new uint32_t(i);
        MsH3Connection connection(*api, CleanUpManual, ClientConnectionCallback, connContext);

        // Setup configuration
        MSH3_CREDENTIAL_CONFIG credConfig = {
            MSH3_CREDENTIAL_TYPE_NONE,
            Config.Flags
        };

        MsH3Configuration config(*api);
        if (!config.IsValid()) {
            printf("[T%u] Failed to create configuration\n", threadId);
            continue;
        }

        config.LoadConfiguration(credConfig);
        connection.SetConfiguration(config);

        // Start the connection
        Metrics[i].StartTime = GetTimestampUs();
        MSH3_STATUS status = connection.Start(config, Config.Host, Config.Address);

        if (MSH3_FAILED(status)) {
            printf("[T%u] Connection %u failed to start with status %u\n",
                   threadId, i, status);
            Metrics[i].EndTime = GetTimestampUs();
            continue;
        }

        // Send requests
        for (uint32_t r = 0; r < Config.RequestsPerConnection && Running; r++) {
            RequestContext* requestCtx = new RequestContext();
            requestCtx->ConnectionIndex = i;
            requestCtx->RequestIndex = r;
            requestCtx->Metrics.StartTime = GetTimestampUs();
            requestCtx->Metrics.EndTime = 0;
            requestCtx->Metrics.BytesSent = 0;
            requestCtx->Metrics.BytesReceived = 0;
            requestCtx->Metrics.Success = true;
              MsH3Request request(connection, MSH3_REQUEST_FLAG_NONE, CleanUpManual, ClientRequestCallback, requestCtx);
            TotalRequests++;
              // Send headers
            size_t headerCount = sizeof(headers) / sizeof(MSH3_HEADER);
            size_t bytesSent = 0;
            for (size_t h = 0; h < headerCount; h++) {
                bytesSent += headers[h].NameLength + headers[h].ValueLength + 2; // +2 for overhead
            }
            requestCtx->Metrics.BytesSent += bytesSent;
            TotalBytesSent += bytesSent;

            status = request.Send(headers, headerCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN);

            if (MSH3_FAILED(status)) {
                printf("[T%u] Request %u on connection %u failed to send headers\n",
                       threadId, r, i);
                requestCtx->Metrics.Success = false;
                FailedRequests++;
            }

            // Space out requests a bit to avoid overwhelming the connection
            if (r < Config.RequestsPerConnection - 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        // Wait for connection to complete
        connection.Shutdown();
    }
}

void RunClientMode() {
    printf("Running in client mode\n");
    printf("Host: %s\n", Config.Host);
    printf("Port: %u\n", ntohs(Config.Address.Addr.Ipv4.sin_port));
    printf("Connections: %u\n", Config.Connections);
    printf("Requests per connection: %u\n", Config.RequestsPerConnection);
    printf("Threads: %u\n", Config.Threads);
    printf("Duration: %u seconds\n", Config.Duration);
      // Initialize metrics
    Metrics.reserve(Config.Connections);
    for (uint32_t i = 0; i < Config.Connections; i++) {
        Metrics.emplace_back();
        Metrics.back().Requests.resize(Config.RequestsPerConnection);
        Metrics.back().SuccessCount = 0;
        Metrics.back().FailureCount = 0;
    }

    // Initialize MSH3 API
    MsH3Api api;
    if (!api) {
        printf("Failed to initialize MSH3 API\n");
        return;
    }

    // Start client worker threads
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < Config.Threads; i++) {
        threads.emplace_back(ClientWorkerThread, i, &api);
    }

    // Run for the specified duration
    auto startTime = std::chrono::steady_clock::now();
    printf("Test running for %u seconds...\n", Config.Duration);

    while (std::chrono::duration_cast<std::chrono::seconds>(
           std::chrono::steady_clock::now() - startTime).count() < Config.Duration) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        printf("Progress: %u/%u requests completed\n",
               CompletedRequests.load(), TotalRequests.load());
    }

    // Signal threads to stop and wait for them
    Running = false;
    printf("Test duration completed, shutting down...\n");

    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    // Print results
    PrintResults();
}

// Server request callback context
struct ServerRequestContext {
    uint32_t ConnectionId;
    uint32_t RequestId;
    RequestMetrics Metrics;
};

// Server request callback
MSH3_STATUS
ServerRequestCallback(
    MsH3Request* Request,
    void* Context,
    MSH3_REQUEST_EVENT* Event
    )
{
    auto* RequestCtx = (ServerRequestContext*)Context;

    switch (Event->Type) {
    case MSH3_REQUEST_EVENT_HEADER_RECEIVED:
        if (Config.Verbose) {
            auto Header = Event->HEADER_RECEIVED.Header;
            printf("[S:C%u:R%u] Header: %.*s: %.*s\n",
                   RequestCtx->ConnectionId,
                   RequestCtx->RequestId,
                   (int)Header->NameLength, Header->Name,
                   (int)Header->ValueLength, Header->Value);
        }
        break;

    case MSH3_REQUEST_EVENT_DATA_RECEIVED:
        {
            RequestCtx->Metrics.BytesReceived += Event->DATA_RECEIVED.Length;
            TotalBytesReceived += Event->DATA_RECEIVED.Length;

            if (Config.Verbose) {
                printf("[S:C%u:R%u] Received %u bytes of data\n",
                       RequestCtx->ConnectionId,
                       RequestCtx->RequestId,
                       Event->DATA_RECEIVED.Length);
            }

            // Acknowledge the data
            MsH3RequestCompleteReceive(Request->Handle, Event->DATA_RECEIVED.Length);
        }
        break;

    case MSH3_REQUEST_EVENT_PEER_SEND_ABORTED:
        if (Config.Verbose) {
            printf("[S:C%u:R%u] Peer send aborted\n",
                   RequestCtx->ConnectionId,
                   RequestCtx->RequestId);
        }
        break;

    case MSH3_REQUEST_EVENT_PEER_RECEIVE_ABORTED:
        if (Config.Verbose) {
            printf("[S:C%u:R%u] Peer receive aborted\n",
                   RequestCtx->ConnectionId,
                   RequestCtx->RequestId);
        }
        RequestCtx->Metrics.Success = false;
        FailedRequests++;
        break;
          case MSH3_REQUEST_EVENT_PEER_SEND_SHUTDOWN:
        {
            if (Config.Verbose) {
                printf("[S:C%u:R%u] Peer send shutdown\n",
                      RequestCtx->ConnectionId,
                      RequestCtx->RequestId);
            }

            // Send a response when the request is fully received
            const char* ResponseData = "HTTP/3 Performance Server Response\n";
            size_t ResponseLength = strlen(ResponseData);

            MSH3_HEADER ResponseHeaders[] = {
                { ":status", 7, "200", 3 },
                { "content-type", 12, "text/plain", 10 },
                { "server", 6, "msh3perf/1.0", 11 }
            };

            // First send headers
            Request->Send(ResponseHeaders,
                         sizeof(ResponseHeaders) / sizeof(MSH3_HEADER),
                         nullptr, 0, MSH3_REQUEST_SEND_FLAG_NONE);

            // Then send data with FIN flag
            Request->Send(nullptr, 0, ResponseData, ResponseLength, MSH3_REQUEST_SEND_FLAG_FIN);

            TotalBytesSent += ResponseLength + 100; // Approximate header size
            RequestCtx->Metrics.BytesSent += ResponseLength + 100;
        }
        break;

    case MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE:
        // Request completed
        RequestCtx->Metrics.EndTime = GetTimestampUs();
        CompletedRequests++;

        if (Config.Verbose) {
            printf("[S:C%u:R%u] Request completed in %.3f ms\n",
                   RequestCtx->ConnectionId,
                   RequestCtx->RequestId,
                   (RequestCtx->Metrics.EndTime - RequestCtx->Metrics.StartTime) / 1000.0);
        }

        if (RequestCtx->Metrics.Success) {
            SuccessfulRequests++;
        }

        delete RequestCtx;
        break;
    }

    return MSH3_STATUS_SUCCESS;
}

// Connection callback for server
MSH3_STATUS
ServerConnectionCallback(
    MsH3Connection* Connection,
    void* Context,
    MSH3_CONNECTION_EVENT* Event
    )
{
    uint32_t connId = *(uint32_t*)Context;

    switch (Event->Type) {
    case MSH3_CONNECTION_EVENT_CONNECTED:
        if (Config.Verbose) {
            printf("[S:C%u] Connected\n", connId);
        }
        break;

    case MSH3_CONNECTION_EVENT_NEW_REQUEST:
        {
            static uint32_t requestId = 0;
            ServerRequestContext* RequestCtx = new ServerRequestContext();
            RequestCtx->ConnectionId = connId;
            RequestCtx->RequestId = requestId++;
            RequestCtx->Metrics.StartTime = GetTimestampUs();
            RequestCtx->Metrics.EndTime = 0;
            RequestCtx->Metrics.BytesSent = 0;
            RequestCtx->Metrics.BytesReceived = 0;
            RequestCtx->Metrics.Success = true;

            TotalRequests++;

            MsH3Request request(Event->NEW_REQUEST.Request, CleanUpManual,
                               ServerRequestCallback, RequestCtx);

            // Request will be handled by the callback
            request.Handle = nullptr; // Prevent auto-closing
        }
        break;

    case MSH3_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        if (Config.Verbose) {
            printf("[S:C%u] Shutdown complete\n", connId);
        }
        delete (uint32_t*)Context;
        break;
    }

    return MSH3_STATUS_SUCCESS;
}

// Server listener callback
MSH3_STATUS
ServerListenerCallback(
    MsH3Listener* Listener,
    void* Context,
    MSH3_LISTENER_EVENT* Event
    )
{
    static uint32_t connectionId = 0;

    switch (Event->Type) {
    case MSH3_LISTENER_EVENT_NEW_CONNECTION:
        {
            uint32_t* connId = new uint32_t(connectionId++);

            // The listener will give ownership of the connection to us
            if (Config.Verbose) {
                printf("[S:L] New connection %u\n", *connId);
            }

            MsH3Connection* connection = new MsH3Connection(
                Event->NEW_CONNECTION.Connection,
                CleanUpAutoDelete, // Delete when shutdown complete
                ServerConnectionCallback,
                connId);

            // Connection handling will be done by the callback
        }
        break;

    case MSH3_LISTENER_EVENT_SHUTDOWN_COMPLETE:
        if (Config.Verbose) {
            printf("[S:L] Listener shutdown complete\n");
        }
        break;
    }

    return MSH3_STATUS_SUCCESS;
}

void RunServerMode() {
    printf("Running in server mode\n");
    printf("Listening on port: %u\n", ntohs(Config.Address.Addr.Ipv4.sin_port));
    printf("Max connections: %u\n", Config.Connections);
    printf("Threads: %u\n", Config.Threads);
    printf("Duration: %u seconds\n", Config.Duration);

    // Initialize MSH3 API
    MsH3Api api;
    if (!api.IsValid()) {
        printf("Failed to initialize MSH3 API\n");
        return;
    }

    // Setup credential configuration
    MSH3_CREDENTIAL_CONFIG credConfig = {
        MSH3_CREDENTIAL_TYPE_NONE,
        Config.Flags
    };

    // Create configuration
    MsH3Configuration config(api);
    if (!config.IsValid()) {
        printf("Failed to create configuration\n");
        return;
    }

    // Load credential configuration
    MSH3_STATUS status = config.LoadConfiguration(credConfig);
    if (MSH3_FAILED(status)) {
        printf("Failed to load credentials, status %u\n", status);
        return;
    }

    // Create listener
    MsH3Listener listener(api, Config.Address, CleanUpManual, ServerListenerCallback);
    if (!listener.IsValid()) {
        printf("Failed to create listener\n");
        return;
    }

    printf("Listener created, waiting for connections...\n");

    // Run for the specified duration
    auto startTime = std::chrono::steady_clock::now();

    while (std::chrono::duration_cast<std::chrono::seconds>(
           std::chrono::steady_clock::now() - startTime).count() < Config.Duration) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        if (TotalRequests.load() > 0) {
            printf("Progress: %u/%u requests completed\n",
                   CompletedRequests.load(), TotalRequests.load());
        }
    }

    printf("Test duration completed, shutting down...\n");

    // Print results
    PrintResults();
}

int main(int argc, char** argv) {
    if (!ParseCommandLine(argc, argv)) {
        return 1;
    }

    if (Config.IsServer) {
        RunServerMode();
    } else {
        RunClientMode();
    }

    return 0;
}
