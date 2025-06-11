/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#define MSH3_TEST_MODE 1
#define MSH3_API_ENABLE_PREVIEW_FEATURES 1

#include "msh3.hpp"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <cstring>
#include <vector>
#include <string>
#include <stdlib.h> // For atoi
#include <algorithm> // For std::min
#include <thread> // For watchdog timer thread
#include <chrono> // For timing
#include <atomic> // For thread communication

// MsQuic parameter constants for testing
#define QUIC_PARAM_CONN_QUIC_VERSION                    0x05000000  // uint32_t
#define QUIC_PARAM_CONN_REMOTE_ADDRESS                  0x05000002  // QUIC_ADDR
#define QUIC_PARAM_STREAM_ID                            0x08000000  // QUIC_UINT62

// MsQuic types for testing
typedef uint64_t QUIC_UINT62;
typedef union QUIC_ADDR {
    struct sockaddr Ip;
    struct sockaddr_in Ipv4;
    struct sockaddr_in6 Ipv6;
} QUIC_ADDR;

// Global flags for command line options
bool g_Verbose = false;
const char* g_TestFilter = nullptr;
uint32_t g_WatchdogTimeoutMs = 5000; // Default to 5 seconds
MsH3Waitable<bool> g_TestAllDone;
std::atomic_int g_ConnectionCount = 0;
MsH3Waitable<bool> g_ConnectionsComplete;

// Helper function to print logs when in verbose mode
#define LOG(...) if (g_Verbose) { printf(__VA_ARGS__); fflush(stdout); }

// Helper function to check if a string matches a pattern with wildcard (*)
bool WildcardMatch(const char* pattern, const char* str) {
    // Case insensitive matching with * wildcard support
    if (!pattern || !str) return false;

    // Empty pattern matches only empty string
    if (*pattern == '\0') return *str == '\0';

    // Special case: '*' matches everything
    if (*pattern == '*' && *(pattern+1) == '\0') return true;

    while (*pattern && *str) {
        if (*pattern == '*') {
            // Skip consecutive wildcards
            while (*(pattern+1) == '*') pattern++;

            // If this is the last character in pattern, we match
            if (*(pattern+1) == '\0') return true;

            // Try to match the rest of the pattern with different positions of str
            pattern++;
            while (*str) {
                if (WildcardMatch(pattern, str)) return true;
                str++;
            }
            return false;
        } else if (tolower(*pattern) == tolower(*str)) {
            // Characters match (case insensitive)
            pattern++;
            str++;
        } else {
            // No match
            return false;
        }
    }

    // Check if remaining pattern consists only of wildcards
    while (*pattern == '*') pattern++;

    return (*pattern == '\0' && *str == '\0');
}

struct TestFunc {
    bool (*Func)(void);
    const char* Name;
};
#define DEF_TEST(X) bool Test##X()
#define ADD_TEST(X) { Test##X, #X }
#define VERIFY(X) \
    do { \
        bool _result = (X); \
        if (g_Verbose) { \
            printf("%s: %s on %s:%d\n", _result ? "PASS" : "FAIL", #X, __FILE__, __LINE__); \
            fflush(stdout); \
        } \
        if (!_result) { \
            fprintf(stderr, #X " Failed on %s:%d!\n", __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)
#define VERIFY_SUCCESS(X) \
    do { \
        auto _status = X; \
        if (g_Verbose) { \
            printf("%s: %s on %s:%d\n", MSH3_FAILED(_status) ? "FAIL" : "PASS", #X, __FILE__, __LINE__); \
            fflush(stdout); \
        } \
        if (MSH3_FAILED(_status)) { \
            fprintf(stderr, #X " Failed with %u on %s:%d!\n", (uint32_t)_status, __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

const MSH3_HEADER RequestHeaders[] = {
    { ":method", 7, "GET", 3 },
    { ":path", 5, "/", 1 },
    { ":scheme", 7, "https", 5 },
    { ":authority", 10, "localhost", 9 },
    { "user-agent", 10, "msh3test", 8 },
    { "accept", 6, "*/*", 3 },
};
const size_t RequestHeadersCount = sizeof(RequestHeaders)/sizeof(MSH3_HEADER);

const MSH3_HEADER ResponseHeaders[] = {
    { ":status", 7, "200", 3 },
    { "content-type", 12, "application/json", 16 },
};
const size_t ResponseHeadersCount = sizeof(ResponseHeaders)/sizeof(MSH3_HEADER);

const char ResponseData[] = "HELLO WORLD!\n";


// Add more types of request headers for testing
const MSH3_HEADER PostRequestHeaders[] = {
    { ":method", 7, "POST", 4 },
    { ":path", 5, "/upload", 7 },
    { ":scheme", 7, "https", 5 },
    { ":authority", 10, "localhost", 9 },
    { "user-agent", 10, "msh3test", 8 },
    { "content-type", 12, "application/json", 16 },
    { "content-length", 14, "13", 2 },
};
const size_t PostRequestHeadersCount = sizeof(PostRequestHeaders)/sizeof(MSH3_HEADER);

const MSH3_HEADER PutRequestHeaders[] = {
    { ":method", 7, "PUT", 3 },
    { ":path", 5, "/resource", 9 },
    { ":scheme", 7, "https", 5 },
    { ":authority", 10, "localhost", 9 },
    { "user-agent", 10, "msh3test", 8 },
    { "content-type", 12, "text/plain", 10 },
    { "content-length", 14, "11", 2 },
};
const size_t PutRequestHeadersCount = sizeof(PutRequestHeaders)/sizeof(MSH3_HEADER);

// Add more response header types for testing
const MSH3_HEADER Response201Headers[] = {
    { ":status", 7, "201", 3 },
    { "location", 8, "/resource/123", 13 },
};
const size_t Response201HeadersCount = sizeof(Response201Headers)/sizeof(MSH3_HEADER);

const MSH3_HEADER Response404Headers[] = {
    { ":status", 7, "404", 3 },
    { "content-type", 12, "text/plain", 10 },
};
const size_t Response404HeadersCount = sizeof(Response404Headers)/sizeof(MSH3_HEADER);

const MSH3_HEADER Response500Headers[] = {
    { ":status", 7, "500", 3 },
    { "content-type", 12, "text/plain", 10 },
};
const size_t Response500HeadersCount = sizeof(Response500Headers)/sizeof(MSH3_HEADER);

const char JsonRequestData[] = "{\"test\":\"data\"}";
const char TextRequestData[] = "Hello World";

const char* ToString(MSH3_CONNECTION_EVENT_TYPE Type) {
    switch (Type) {
        case MSH3_CONNECTION_EVENT_CONNECTED: return "CONNECTED";
        case MSH3_CONNECTION_EVENT_NEW_REQUEST: return "NEW_REQUEST";
        case MSH3_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT: return "SHUTDOWN_INITIATED_BY_TRANSPORT";
        case MSH3_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER: return "SHUTDOWN_INITIATED_BY_PEER";
        case MSH3_CONNECTION_EVENT_SHUTDOWN_COMPLETE: return "SHUTDOWN_COMPLETE";
        default: return "UNKNOWN";
    }
}

const char* ToString(MSH3_REQUEST_EVENT_TYPE Type) {
    switch (Type) {
        case MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE: return "SHUTDOWN_COMPLETE";
        case MSH3_REQUEST_EVENT_HEADER_RECEIVED: return "HEADER_RECEIVED";
        case MSH3_REQUEST_EVENT_DATA_RECEIVED: return "DATA_RECEIVED";
        case MSH3_REQUEST_EVENT_PEER_SEND_SHUTDOWN: return "PEER_SEND_SHUTDOWN";
        case MSH3_REQUEST_EVENT_PEER_SEND_ABORTED: return "PEER_SEND_ABORTED";
        case MSH3_REQUEST_EVENT_IDEAL_SEND_SIZE: return "IDEAL_SEND_SIZE";
        case MSH3_REQUEST_EVENT_SEND_COMPLETE: return "SEND_COMPLETE";
        case MSH3_REQUEST_EVENT_SEND_SHUTDOWN_COMPLETE: return "SEND_SHUTDOWN_COMPLETE";
        case MSH3_REQUEST_EVENT_PEER_RECEIVE_ABORTED: return "PEER_RECEIVE_ABORTED";
        default: return "UNKNOWN";
    }
}

struct TestRequest : public MsH3Request {
    const char* Role;
    TestRequest(MsH3Connection& Connection, MsH3CleanUpMode CleanUpMode = CleanUpManual)
     : MsH3Request(Connection, MSH3_REQUEST_FLAG_NONE, CleanUpMode, RequestCallback, this), Role("CLIENT") {
        LOG("%s TestRequest constructed\n", Role);
    }
    TestRequest(
        MSH3_REQUEST* ServerHandle,
        MsH3CleanUpMode CleanUpMode
        ) noexcept : MsH3Request(ServerHandle, CleanUpMode, RequestCallback, this), Role("SERVER") {
        LOG("%s TestRequest constructed\n", Role);
    }
    ~TestRequest() noexcept { LOG("~TestRequest\n"); }

    struct StoredHeader {
        std::string Name;
        std::string Value;
        StoredHeader(const char* name, size_t nameLen, const char* value, size_t valueLen)
            : Name(name, nameLen), Value(value, valueLen) {}
    };

    // Set of all the headers received
    std::vector<StoredHeader> Headers;
    MsH3Waitable<bool> AllDataSent;         // Signal when all data has been sent
    MsH3Waitable<bool> AllHeadersReceived;  // Signal when headers are complete (data received)
    MsH3Waitable<bool> AllDataReceived;     // Signal when all data has been received
    MsH3Waitable<uint32_t> LatestDataReceived;  // Signal when latest data has been received
    uint64_t TotalDataReceived = 0;         // Total data received in bytes
    bool PeerSendComplete = false;          // Flag to track if peer send was gracefully completed
    bool PeerSendAborted = false;           // Flag to track if peer send was aborted
    bool HandleReceivesAsync = false;
    bool CompleteAsyncReceivesInline = false;

    // Helper to get the first header by name
    StoredHeader* GetHeaderByName(const char* name, size_t nameLength) {
        std::string nameStr(name, nameLength);
        for (auto& header : Headers) {
            if (header.Name == nameStr) {
                return &header;
            }
        }
        return nullptr;
    }

    // Check if we have a specific number of headers
    bool HasExpectedHeaderCount(uint32_t expected) {
        return Headers.size() == expected;
    }

    // Helper to get the status code from headers
    uint32_t GetStatusCode() {
        auto statusHeader = GetHeaderByName(":status", 7);
        if (statusHeader && !statusHeader->Value.empty()) {
            // Convert status code string to integer
            return atoi(statusHeader->Value.c_str());
        }
        return 0; // Return 0 if status header not found or invalid
    }

    static MSH3_STATUS RequestCallback(
        struct MsH3Request* Request,
        void* Context,
        MSH3_REQUEST_EVENT* Event
    ) {
        // First check if input is valid
        if (!Request || !Context || !Event) {
            LOG("Warning: Invalid RequestCallback input\n");
            return MSH3_STATUS_SUCCESS;
        }

        auto ctx = (TestRequest*)Context;
        LOG("%s RequestEvent: %s\n", ctx->Role, ToString(Event->Type));

        if (Event->Type == MSH3_REQUEST_EVENT_HEADER_RECEIVED) {
            const MSH3_HEADER* header = Event->HEADER_RECEIVED.Header;

            // Validate the header
            if (!header || !header->Name || header->NameLength == 0 || !header->Value) {
                LOG("%s Warning: Received invalid header\n", ctx->Role);
                return MSH3_STATUS_SUCCESS;
            }

            // Save the header data
            ctx->Headers.emplace_back(
                header->Name, header->NameLength, header->Value, header->ValueLength);

            LOG("%s Processed header: '%s'\n", ctx->Role, ctx->Headers.back().Name.c_str());

        } else if (Event->Type == MSH3_REQUEST_EVENT_DATA_RECEIVED) {
            LOG("%s Data received: %u bytes\n", ctx->Role, Event->DATA_RECEIVED.Length);
            if (!ctx->AllHeadersReceived.Get()) {
                // Signal that all headers have been received (since data always comes after headers)
                LOG("%s Request headers complete\n", ctx->Role);
                ctx->AllHeadersReceived.Set(true);
            }

            ctx->TotalDataReceived += Event->DATA_RECEIVED.Length;
            ctx->LatestDataReceived.Set(Event->DATA_RECEIVED.Length);

            if (ctx->HandleReceivesAsync) {
                if (ctx->CompleteAsyncReceivesInline) {
                    LOG("%s Completing async receive inline\n", ctx->Role);
                    Request->CompleteReceive(Event->DATA_RECEIVED.Length);
                }
                return MSH3_STATUS_PENDING;
            }

        } else if (Event->Type == MSH3_REQUEST_EVENT_PEER_SEND_SHUTDOWN) {
            if (!ctx->AllHeadersReceived.Get()) {
                // Signal that all headers have been received (since data always comes after headers)
                LOG("%s Request headers complete\n", ctx->Role);
                ctx->AllHeadersReceived.Set(true);
            }
            ctx->PeerSendComplete = true;
            ctx->AllDataReceived.Set(true);

        } else if (Event->Type == MSH3_REQUEST_EVENT_PEER_SEND_ABORTED) {
            // Handle case where request completes without data
            ctx->PeerSendAborted = true;
            if (!ctx->AllHeadersReceived.Get()) {
                // Signal that all headers have been received (since data always comes after headers)
                LOG("%s Request headers complete\n", ctx->Role);
                ctx->AllHeadersReceived.Set(true);
            }
            ctx->AllDataReceived.Set(true);

        } else if (Event->Type == MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE) {
            if (!ctx->AllHeadersReceived.Get()) {
                // Signal that all headers have been received (since data always comes after headers)
                LOG("%s Request headers complete\n", ctx->Role);
                ctx->AllHeadersReceived.Set(true);
            }
            if (!ctx->AllDataReceived.Get()) {
                // Signal that all headers have been received (since data always comes after headers)
                LOG("%s Data complete\n", ctx->Role);
                ctx->AllDataReceived.Set(true);
            }
        } else if (Event->Type == MSH3_REQUEST_EVENT_SEND_SHUTDOWN_COMPLETE) {
            if (!ctx->AllDataSent.Get()) {
                ctx->AllDataSent.Set(true);
            }
        }

        return MSH3_STATUS_SUCCESS;
    }
};

struct TestConnection : public MsH3Connection {
    TestConnection(
        MsH3Api& Api,
        MsH3CleanUpMode CleanUpMode = CleanUpManual,
        MsH3ConnectionCallback* Callback = NoOpCallback,
        void* Context = nullptr
        ) noexcept : MsH3Connection(Api, CleanUpMode, Callback, Context) {
        g_ConnectionCount.fetch_add(1);
        LOG("TestConnection created (client)\n");
    }
    TestConnection(
        MSH3_CONNECTION* ServerHandle,
        MsH3CleanUpMode CleanUpMode,
        MsH3ConnectionCallback* Callback,
        void* Context = nullptr
        ) noexcept : MsH3Connection(ServerHandle, CleanUpMode, Callback, Context) {
        g_ConnectionCount.fetch_add(1);
        LOG("TestConnection created (server)\n");
    }
    virtual ~TestConnection() noexcept {
        if (g_ConnectionCount.fetch_sub(1) == 1) {
            LOG("All connections closed, signaling completion\n");
            g_ConnectionsComplete.Set(true);
        }
        LOG("~TestConnection\n");
    }
};

struct TestServer : public MsH3Listener {
    bool AutoConfigure; // Automatically configure the server
    MsH3Configuration Configuration;
    MsH3Waitable<TestConnection*> NewConnection;
    MsH3Waitable<TestRequest*> NewRequest;
    TestServer(MsH3Api& Api, bool AutoConfigure = true)
     : MsH3Listener(Api, MsH3Addr(), CleanUpAutoDelete, ListenerCallback, this), Configuration(Api), AutoConfigure(AutoConfigure) {
        if (Handle && MSH3_FAILED(Configuration.LoadConfiguration())) {
            MsH3ListenerClose(Handle); Handle = nullptr;
        }
    }
    ~TestServer() noexcept { LOG("~TestServer\n"); }
    bool WaitForConnection() noexcept {
        VERIFY(NewConnection.WaitFor());
        auto ServerConnection = NewConnection.Get();
        VERIFY(ServerConnection->Connected.WaitFor());
        return true;
    }
    static
    MSH3_STATUS
    ListenerCallback(
        MsH3Listener* /* Listener */,
        void* Context,
        MSH3_LISTENER_EVENT* Event
        ) noexcept {
        auto pThis = (TestServer*)Context;
        MSH3_STATUS Status = MSH3_STATUS_INVALID_STATE;
        if (Event->Type == MSH3_LISTENER_EVENT_NEW_CONNECTION) {
            auto Connection = new(std::nothrow) TestConnection(Event->NEW_CONNECTION.Connection, CleanUpAutoDelete, ConnectionCallback, pThis);
            if (Connection) {
                if (pThis->AutoConfigure &&
                    MSH3_FAILED(Status = Connection->SetConfiguration(pThis->Configuration))) {
                    //
                    // The connection is being rejected. Let the library free the handle.
                    //
                    Connection->Handle = nullptr;
                    delete Connection;
                } else {
                    Status = MSH3_STATUS_SUCCESS;
                    pThis->NewConnection.Set(Connection);
                }
            }
        }
        return Status;
    }
    static
    MSH3_STATUS
    ConnectionCallback(
        MsH3Connection* /* Connection */,
        void* Context,
        MSH3_CONNECTION_EVENT* Event
        ) noexcept {
        auto pThis = (TestServer*)Context;
        LOG("SERVER ConnectionEvent: %s\n", ToString(Event->Type));
        if (Event->Type == MSH3_CONNECTION_EVENT_NEW_REQUEST) {
            auto Request = new (std::nothrow) TestRequest(Event->NEW_REQUEST.Request, CleanUpAutoDelete);
            pThis->NewRequest.Set(Request);
        }
        return MSH3_STATUS_SUCCESS;
    }
};

const MSH3_CREDENTIAL_CONFIG ClientCredConfig = {
    MSH3_CREDENTIAL_TYPE_NONE,
    MSH3_CREDENTIAL_FLAG_CLIENT | MSH3_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION,
    nullptr
};

struct TestClient : public TestConnection {
    bool SingleThreaded;
    MsH3Configuration Config;
    TestClient(MsH3Api& Api, bool SingleThread = false, MsH3CleanUpMode CleanUpMode = CleanUpManual)
        : TestConnection(Api, CleanUpMode, Callbacks), Config(Api), SingleThreaded(SingleThread) {
        if (Handle && MSH3_FAILED(Config.LoadConfiguration(ClientCredConfig))) {
            MsH3ConnectionClose(Handle); Handle = nullptr;
        }
    }
    virtual ~TestClient() noexcept {
        Shutdown();
        Close();
    }
    MSH3_STATUS Start() noexcept { return MsH3Connection::Start(Config); }
    static
    MSH3_STATUS
    Callbacks(
        MsH3Connection* Connection,
        void* /* Context */,
        MSH3_CONNECTION_EVENT* Event
        ) noexcept {
        LOG("CLIENT ConnectionEvent: %s\n", ToString(Event->Type));
        if (Event->Type == MSH3_CONNECTION_EVENT_NEW_REQUEST) {
            //
            // Not great beacuse it doesn't provide an application specific
            // error code. If you expect to get streams, you should not no-op
            // the callbacks.
            //
            MsH3RequestClose(Event->NEW_REQUEST.Request);
        } else if (Event->Type == MSH3_CONNECTION_EVENT_CONNECTED) {
            if (((TestClient*)Connection)->SingleThreaded) {
                Connection->Shutdown();
            }
        }
        return MSH3_STATUS_SUCCESS;
    }
};

DEF_TEST(Handshake) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    Client.Shutdown();
    VERIFY(Client.ShutdownComplete.WaitFor());
    return true;
}

DEF_TEST(HandshakeSingleThread) {
    MsH3EventQueue EventQueue; VERIFY(EventQueue.IsValid());
    MSH3_EXECUTION_CONFIG ExecutionConfig = { 0, EventQueue };
    MSH3_EXECUTION* Execution;
    MsH3Api Api(1, &ExecutionConfig, &Execution); VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    auto Client = new TestClient(Api, true, CleanUpAutoDelete);
    VERIFY(Client->IsValid());
    VERIFY_SUCCESS(Client->Start());
    uint32_t DrainCount = 10;
    while (!g_ConnectionsComplete.Get() && DrainCount-- > 0) {
        uint32_t WaitTime = Api.Poll(Execution);
        EventQueue.CompleteEvents(g_ConnectionsComplete.Get() ? 100 : WaitTime);
    }
    return true;
}

DEF_TEST(HandshakeFail) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    VERIFY_SUCCESS(Client.Start());
    VERIFY(!Client.Connected.WaitFor(1000));
    return true;
}

DEF_TEST(HandshakeSetCertTimeout) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api, false); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.NewConnection.WaitFor());
    VERIFY(!Server.NewConnection.Get()->Connected.WaitFor(1000));
    VERIFY(!Client.Connected.WaitFor());
    return true;
}

DEF_TEST(SimpleRequest) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    TestRequest Request(Client); VERIFY(Request.IsValid());
    VERIFY(Request.Send(RequestHeaders, RequestHeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    VERIFY(Server.NewRequest.WaitFor());
    auto ServerRequest = Server.NewRequest.Get();
    ServerRequest->Shutdown(MSH3_REQUEST_SHUTDOWN_FLAG_GRACEFUL);
    VERIFY(Request.ShutdownComplete.WaitFor());
    return true;
}

bool ReceiveData(bool Async, bool Inline = true) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    TestRequest Request(Client);
    Request.HandleReceivesAsync = Async;
    Request.CompleteAsyncReceivesInline = Inline;
    VERIFY(Request.Send(RequestHeaders, RequestHeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(Request.IsValid());
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    VERIFY(Server.NewRequest.WaitFor());
    auto ServerRequest = Server.NewRequest.Get();
    VERIFY(ServerRequest->Send(RequestHeaders, RequestHeadersCount, ResponseData, sizeof(ResponseData), MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(Request.LatestDataReceived.WaitFor());
    VERIFY(Request.LatestDataReceived.Get() == sizeof(ResponseData));
    if (Async && !Inline) {
        Request.CompleteReceive(Request.LatestDataReceived.Get());
    }
    VERIFY(Request.ShutdownComplete.WaitFor());
    return true;
}

DEF_TEST(ReceiveDataInline) {
    return ReceiveData(false);
}

DEF_TEST(ReceiveDataAsync) {
    return ReceiveData(true, false);
}

DEF_TEST(ReceiveDataAsyncInline) {
    return ReceiveData(true, true);
}

DEF_TEST(HeaderValidation) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    LOG("Connection established\n");

    TestRequest Request(Client);
    VERIFY(Request.IsValid());
    LOG("Request created\n");

    // Send a request with valid headers
    LOG("Sending request with headers\n");
    VERIFY(Request.Send(RequestHeaders, RequestHeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
    LOG("Request sent, waiting for server to receive it\n");
    VERIFY(Server.NewRequest.WaitFor());
    auto ServerRequest = Server.NewRequest.Get();
    LOG("Server received request\n");

    // Server sends response with headers
    LOG("Server sending response\n");
    VERIFY(ServerRequest->Send(ResponseHeaders, ResponseHeadersCount, ResponseData, sizeof(ResponseData), MSH3_REQUEST_SEND_FLAG_FIN));
    LOG("Response sent\n");

    // Wait for all headers to be received (signaled by data event or completion)
    LOG("Waiting for all headers to be received\n");
    VERIFY(Request.AllHeadersReceived.WaitFor());
    LOG("All headers received\n");

    LOG("Header count received: %zu\n", Request.Headers.size());
    VERIFY(Request.Headers.size() == ResponseHeadersCount);
    LOG("Successfully received the expected number of headers\n");

    // Verify the header data we copied
    LOG("Verifying header data\n");

    // Log how many headers we received
    LOG("Received %zu headers\n", Request.Headers.size());
    for (size_t i = 0; i < Request.Headers.size(); i++) {
        LOG("  Header[%zu]: %s = %s\n", i,
            Request.Headers[i].Name.c_str(),
            Request.Headers[i].Value.c_str());
    }

    // Check for the status header and verify it
    auto statusHeader = Request.GetHeaderByName(":status", 7);
    VERIFY(statusHeader != nullptr);

    // Verify header name
    VERIFY(statusHeader->Name == ":status");
    LOG("Header name verified\n");

    // Verify header value
    VERIFY(statusHeader->Value == "200");
    LOG("Header value verified\n");

    return true;
}

DEF_TEST(DifferentResponseCodes) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    LOG("Connection established for DifferentResponseCodes test\n");

    // Test 201 Created response
    {
        LOG("Testing 201 Created response\n");
        TestRequest Request(Client);

        LOG("Sending PUT request\n");
        VERIFY(Request.Send(PutRequestHeaders, PutRequestHeadersCount, TextRequestData, sizeof(TextRequestData) - 1, MSH3_REQUEST_SEND_FLAG_FIN));
        LOG("Waiting for server to receive request\n");
        VERIFY(Server.NewRequest.WaitFor());
        auto ServerRequest = Server.NewRequest.Get();
        LOG("Server request received\n");

        // Server sends 201 Created response
        LOG("Sending 201 response\n");
        VERIFY(ServerRequest->Send(Response201Headers, Response201HeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
        LOG("Response sent\n");

        // Validate the received headers
        LOG("Waiting for all headers\n");
        VERIFY(Request.AllHeadersReceived.WaitFor());

        // Verify status code
        LOG("Verifying status code\n");
        uint32_t statusCode = Request.GetStatusCode();
        LOG("Status code received: %u\n", statusCode);
        VERIFY(statusCode == 201);

        // Verify location header
        auto locationHeader = Request.GetHeaderByName("location", 8);
        VERIFY(locationHeader != nullptr);
        VERIFY(locationHeader->Value == "/resource/123");

        LOG("201 Created test passed\n");
    }

    // Test 404 Not Found response
    {
        LOG("Testing 404 Not Found response\n");
        // Reset the server's NewRequest waitable
        Server.NewRequest.Reset();

        TestRequest Request(Client);

        LOG("Sending GET request for 404\n");
        VERIFY(Request.Send(RequestHeaders, RequestHeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
        LOG("404 Request sent, waiting for server\n");

        VERIFY(Server.NewRequest.WaitFor());
        auto ServerRequest = Server.NewRequest.Get();
        LOG("404 Server request received\n");

        // Server sends 404 Not Found response
        const char notFoundBody[] = "Not Found";
        LOG("Sending 404 response\n");
        VERIFY(ServerRequest->Send(Response404Headers, Response404HeadersCount, notFoundBody, sizeof(notFoundBody) - 1, MSH3_REQUEST_SEND_FLAG_FIN));
        LOG("404 Response sent\n");

        // Validate the received status code
        LOG("Waiting for all headers\n");
        VERIFY(Request.AllHeadersReceived.WaitFor());

        // Verify status code
        uint32_t statusCode = Request.GetStatusCode();
        LOG("Status code received: %u\n", statusCode);
        VERIFY(statusCode == 404);

        // Verify content-type header
        auto contentTypeHeader = Request.GetHeaderByName("content-type", 12);
        VERIFY(contentTypeHeader != nullptr);
        VERIFY(contentTypeHeader->Value == "text/plain");

        LOG("404 Not Found test passed\n");
    }

    // Test 500 Internal Server Error response
    {
        LOG("Testing 500 Internal Server Error response\n");
        // Reset the server's NewRequest waitable
        Server.NewRequest.Reset();

        TestRequest Request(Client);

        LOG("Sending request for 500\n");
        VERIFY(Request.Send(RequestHeaders, RequestHeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
        LOG("500 Request sent\n");

        VERIFY(Server.NewRequest.WaitFor());
        auto ServerRequest = Server.NewRequest.Get();
        LOG("500 Server request received\n");

        // Server sends 500 Internal Server Error response
        const char serverErrorBody[] = "Server Error";
        LOG("Sending 500 response\n");
        VERIFY(ServerRequest->Send(Response500Headers, Response500HeadersCount, serverErrorBody, sizeof(serverErrorBody) - 1, MSH3_REQUEST_SEND_FLAG_FIN));
        LOG("500 Response sent\n");

        // Validate the received status code
        LOG("Waiting for all headers\n");
        VERIFY(Request.AllHeadersReceived.WaitFor());

        // Verify status code
        uint32_t statusCode = Request.GetStatusCode();
        LOG("Status code received: %u\n", statusCode);
        VERIFY(statusCode == 500);

        // Verify content-type header
        auto contentTypeHeader = Request.GetHeaderByName("content-type", 12);
        VERIFY(contentTypeHeader != nullptr);
        VERIFY(contentTypeHeader->Value == "text/plain");

        LOG("500 Internal Server Error test passed\n");
    }
    return true;
}

DEF_TEST(MultipleRequests) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());

    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());

    LOG("Connection established, starting requests\n");

    // Send first request (GET)
    LOG("Sending first request (GET)\n");
    TestRequest Request1(Client);
    VERIFY(Request1.Send(RequestHeaders, RequestHeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(Server.NewRequest.WaitFor());
    auto ServerRequest1 = Server.NewRequest.Get();
    LOG("First server request received\n");

    VERIFY(ServerRequest1->Send(ResponseHeaders, ResponseHeadersCount, ResponseData, sizeof(ResponseData), MSH3_REQUEST_SEND_FLAG_FIN));
    LOG("First response sent\n");

    // Wait for headers and validate status code
    VERIFY(Request1.AllHeadersReceived.WaitFor());
    uint32_t statusCode1 = Request1.GetStatusCode();
    LOG("First status code received: %u\n", statusCode1);
    VERIFY(statusCode1 == 200);

    // Verify content-type header
    auto contentType1 = Request1.GetHeaderByName("content-type", 12);
    VERIFY(contentType1 != nullptr);
    VERIFY(contentType1->Value == "application/json");
    LOG("First request headers validated\n");

    // Send second request (POST)
    LOG("Sending second request (POST)\n");
    Server.NewRequest.Reset();
    TestRequest Request2(Client);
    VERIFY(Request2.Send(PostRequestHeaders, PostRequestHeadersCount, JsonRequestData, sizeof(JsonRequestData) - 1, MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(Server.NewRequest.WaitFor());
    auto ServerRequest2 = Server.NewRequest.Get();
    LOG("Second server request received\n");

    VERIFY(ServerRequest2->Send(Response201Headers, Response201HeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
    LOG("Second response sent\n");

    // Wait for headers and validate status code
    VERIFY(Request2.AllHeadersReceived.WaitFor());
    uint32_t statusCode2 = Request2.GetStatusCode();
    LOG("Second status code received: %u\n", statusCode2);
    VERIFY(statusCode2 == 201);

    // Verify location header
    auto locationHeader = Request2.GetHeaderByName("location", 8);
    VERIFY(locationHeader != nullptr);
    VERIFY(locationHeader->Value == "/resource/123");
    LOG("Second request headers validated\n");

    // Send third request (PUT)
    LOG("Sending third request (PUT)\n");
    Server.NewRequest.Reset();
    TestRequest Request3(Client);
    VERIFY(Request3.Send(PutRequestHeaders, PutRequestHeadersCount, TextRequestData, sizeof(TextRequestData) - 1, MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(Server.NewRequest.WaitFor());
    auto ServerRequest3 = Server.NewRequest.Get();
    LOG("Third server request received\n");

    VERIFY(ServerRequest3->Send(ResponseHeaders, ResponseHeadersCount, ResponseData, sizeof(ResponseData), MSH3_REQUEST_SEND_FLAG_FIN));
    LOG("Third response sent\n");

    // Wait for headers and validate status code
    VERIFY(Request3.AllHeadersReceived.WaitFor());
    uint32_t statusCode3 = Request3.GetStatusCode();
    LOG("Third status code received: %u\n", statusCode3);
    VERIFY(statusCode3 == 200);

    // Verify content-type header for third request
    auto contentType3 = Request3.GetHeaderByName("content-type", 12);
    VERIFY(contentType3 != nullptr);
    VERIFY(contentType3->Value == "application/json");
    LOG("Third request headers validated\n");
    return true;
}

bool RequestTransferTest(uint32_t Upload, uint32_t Download) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    TestRequest Request(Client);
    // Send out the requested data on upload
    std::vector<uint8_t> RequestData(Upload, 0xEF);
    VERIFY(Request.Send(RequestHeaders, RequestHeadersCount, RequestData.data(), RequestData.size(), MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(Request.IsValid());
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    // Wait for the server to receive the request w/ payload
    VERIFY(Server.NewRequest.WaitFor());
    auto ServerRequest = Server.NewRequest.Get();
    VERIFY(ServerRequest->AllDataReceived.WaitFor(2000)); // A bit longer wait for data
    VERIFY(ServerRequest->PeerSendComplete);
    VERIFY(ServerRequest->TotalDataReceived == Upload);
    // Send the response data on download
    std::vector<uint8_t> ResponseData(Download, 0xAB);
    VERIFY(ServerRequest->Send(ResponseHeaders, ResponseHeadersCount, ResponseData.data(), ResponseData.size(), MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(Request.AllDataReceived.WaitFor(2000)); // A bit longer wait for data
    VERIFY(Request.PeerSendComplete);
    VERIFY(Request.TotalDataReceived == Download);
    return true;
}

// Throughput test sizes
#define LARGE_TEST_SIZE_1MB (1024 * 1024)
#define LARGE_TEST_SIZE_10MB (10 * 1024 * 1024)
#define LARGE_TEST_SIZE_50MB (50 * 1024 * 1024)
#define LARGE_TEST_SIZE_100MB (100 * 1024 * 1024)

DEF_TEST(RequestDownload1MB) {
    return RequestTransferTest(0, LARGE_TEST_SIZE_1MB);
}

DEF_TEST(RequestDownload10MB) {
    return RequestTransferTest(0, LARGE_TEST_SIZE_10MB);
}

DEF_TEST(RequestDownload50MB) {
    return RequestTransferTest(0, LARGE_TEST_SIZE_50MB);
}

DEF_TEST(RequestUpload1MB) {
    return RequestTransferTest(LARGE_TEST_SIZE_1MB, 0);
}

DEF_TEST(RequestUpload10MB) {
    return RequestTransferTest(LARGE_TEST_SIZE_10MB, 0);
}

DEF_TEST(RequestUpload50MB) {
    return RequestTransferTest(LARGE_TEST_SIZE_50MB, 0);
}

DEF_TEST(RequestBidirectional10MB) {
    return RequestTransferTest(LARGE_TEST_SIZE_10MB, LARGE_TEST_SIZE_10MB);
}

DEF_TEST(ConnectionGetQuicParam) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    
    // Test getting QUIC_PARAM_CONN_QUIC_VERSION
    uint32_t QuicVersion = 0;
    uint32_t BufferLength = sizeof(QuicVersion);
    auto Status = MsH3ConnectionGetQuicParam(Client.Handle, QUIC_PARAM_CONN_QUIC_VERSION, &BufferLength, &QuicVersion);
    VERIFY_SUCCESS(Status);
    VERIFY(BufferLength == sizeof(QuicVersion));
    VERIFY(QuicVersion != 0); // Should have a valid QUIC version
    
    // Test getting QUIC_PARAM_CONN_REMOTE_ADDRESS
    QUIC_ADDR RemoteAddr;
    BufferLength = sizeof(RemoteAddr);
    Status = MsH3ConnectionGetQuicParam(Client.Handle, QUIC_PARAM_CONN_REMOTE_ADDRESS, &BufferLength, &RemoteAddr);
    VERIFY_SUCCESS(Status);
    VERIFY(BufferLength == sizeof(RemoteAddr));
    
    Client.Shutdown();
    VERIFY(Client.ShutdownComplete.WaitFor());
    return true;
}

DEF_TEST(RequestGetQuicParam) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    TestRequest Request(Client); VERIFY(Request.IsValid());
    VERIFY(Request.Send(RequestHeaders, RequestHeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    VERIFY(Server.NewRequest.WaitFor());
    
    // Test getting QUIC_PARAM_STREAM_ID
    QUIC_UINT62 StreamId = 0;
    uint32_t BufferLength = sizeof(StreamId);
    auto Status = MsH3RequestGetQuicParam(Request.Handle, QUIC_PARAM_STREAM_ID, &BufferLength, &StreamId);
    VERIFY_SUCCESS(Status);
    VERIFY(BufferLength == sizeof(StreamId));
    VERIFY(StreamId != 0); // Should have a valid stream ID
    
    auto ServerRequest = Server.NewRequest.Get();
    ServerRequest->Shutdown(MSH3_REQUEST_SHUTDOWN_FLAG_GRACEFUL);
    VERIFY(Request.ShutdownComplete.WaitFor());
    return true;
}

DEF_TEST(GetQuicParamBasic) {
    // Basic test to verify the functions exist and handle null parameters correctly
    MsH3Api Api; VERIFY(Api.IsValid());
    
    // Test with null connection should fail appropriately
    uint32_t bufferLength = sizeof(uint32_t);
    uint32_t buffer = 0;
    auto status = MsH3ConnectionGetQuicParam(nullptr, QUIC_PARAM_CONN_QUIC_VERSION, &bufferLength, &buffer);
    VERIFY(MSH3_FAILED(status)); // Should fail gracefully with null connection
    
    // Test with null request should fail appropriately  
    status = MsH3RequestGetQuicParam(nullptr, QUIC_PARAM_STREAM_ID, &bufferLength, &buffer);
    VERIFY(MSH3_FAILED(status)); // Should fail gracefully with null request
    
    return true;
}

const TestFunc TestFunctions[] = {
    ADD_TEST(Handshake),
    //ADD_TEST(HandshakeSingleThread),
    ADD_TEST(HandshakeFail),
    ADD_TEST(HandshakeSetCertTimeout),
    ADD_TEST(SimpleRequest),
    ADD_TEST(ReceiveDataInline),
    ADD_TEST(ReceiveDataAsync),
    ADD_TEST(ReceiveDataAsyncInline),
    ADD_TEST(HeaderValidation),
    ADD_TEST(DifferentResponseCodes),
    ADD_TEST(MultipleRequests),
    ADD_TEST(GetQuicParamBasic),
    ADD_TEST(ConnectionGetQuicParam),
    ADD_TEST(RequestGetQuicParam),
    ADD_TEST(RequestDownload1MB),
    ADD_TEST(RequestDownload10MB),
    ADD_TEST(RequestDownload50MB),
    ADD_TEST(RequestUpload1MB),
    ADD_TEST(RequestUpload10MB),
    ADD_TEST(RequestUpload50MB),
    ADD_TEST(RequestBidirectional10MB),
};
const uint32_t TestCount = sizeof(TestFunctions)/sizeof(TestFunc);

// Simple watchdog function: wait for test completion or exit on timeout
void WatchdogFunction() {
    LOG("Watchdog started with timeout %u ms\n", g_WatchdogTimeoutMs);
    if (!g_TestAllDone.WaitFor(g_WatchdogTimeoutMs)) {
        printf("WATCHDOG TIMEOUT! Killing process...\n"); fflush(stdout);
        exit(1); // Exit if test takes too long
    }
    LOG("Watchdog completed successfully\n");
}

// Helper function to check if a character is a quote (single, double, or backtick)
static inline bool IsQuoteChar(char c) {
    return c == '"' || c == '\'';
}

void PrintUsage(const char* program) {
    printf("Usage: %s [options]\n", program);
    printf("Options:\n");
    printf("  -f, --filter=PATTERN  Only run tests matching pattern (supports * wildcard)\n");
    printf("  -h, --help            Print this help message\n");
    printf("  -v, --verbose         Print detailed test information\n");
    printf("  -t, --timeout=MSEC    Set watchdog timeout in milliseconds (default: 5000)\n");
}

int MSH3_CALL main(int argc, char** argv) {
    // Parse command line options
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_Verbose = true;
        } else if (strncmp(argv[i], "-f=", 3) == 0 || strncmp(argv[i], "--filter=", 9) == 0) {
            char* filterValue = (argv[i][1] == 'f') ? argv[i] + 3 : argv[i] + 9;
            // If the filter value starts with a quote, remove the quotes
            if (filterValue[0] != '\0' && IsQuoteChar(filterValue[0])) {
                char quoteChar = filterValue[0];
                size_t len = strlen(filterValue);
                // Check if it ends with the same quote
                if (len > 1 && filterValue[len-1] == quoteChar) {
                    // Skip the first quote and null-terminate before the last quote
                    // We're modifying argv directly which is allowed
                    filterValue[len-1] = '\0';
                    filterValue++;
                }
            }
            g_TestFilter = filterValue;
        } else if (strncmp(argv[i], "-t=", 3) == 0 || strncmp(argv[i], "--timeout=", 10) == 0) {
            const char* timeoutValue = (argv[i][1] == 't') ? argv[i] + 3 : argv[i] + 10;
            // Convert timeout value to integer
            int timeout = atoi(timeoutValue);
            if (timeout > 0) {
                g_WatchdogTimeoutMs = (uint32_t)timeout;
            } else {
                printf("Invalid timeout value: %s\n", timeoutValue);
                PrintUsage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "-?") == 0 || strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            PrintUsage(argv[0]);
            return 0;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            PrintUsage(argv[0]);
            return 1;
        }
    }

    // Calculate how many tests will run with filter
    uint32_t runCount = 0;
    if (g_TestFilter) {
        for (uint32_t i = 0; i < TestCount; ++i) {
            if (WildcardMatch(g_TestFilter, TestFunctions[i].Name)) {
                runCount++;
            }
        }
        printf("Running %u/%u tests matching filter: %s\n", runCount, TestCount, g_TestFilter);
    } else {
        runCount = TestCount;
        printf("Running %u tests\n", TestCount);
    }

    // If no tests match the filter, return early
    if (runCount == 0) {
        printf("No tests match the specified filter\n");
        return 1;
    }

    // Run the tests
    uint32_t FailCount = 0;
    for (uint32_t i = 0; i < TestCount; ++i) {
        // Skip tests that don't match the filter
        if (g_TestFilter && !WildcardMatch(g_TestFilter, TestFunctions[i].Name)) {
            continue;
        }

        printf("  %s\n", TestFunctions[i].Name);
        fflush(stdout);

        // Start watchdog thread for this test
        g_TestAllDone.Reset();
        std::thread watchdogThread(WatchdogFunction);

        auto result = TestFunctions[i].Func();
        LOG("Completed test: %s - %s\n", TestFunctions[i].Name, result ? "PASSED" : "FAILED");

        g_TestAllDone.Set(true);

        // Wait for watchdog thread to finish
        if (watchdogThread.joinable()) {
            watchdogThread.join();
        }

        if (!result) FailCount++;
    }

    printf("Complete! %u test%s failed\n", FailCount, FailCount == 1 ? "" : "s");
    return FailCount ? 1 : 0;
}
