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

// Global flags for command line options
bool g_Verbose = false;
const char* g_TestFilter = nullptr;
uint32_t g_WatchdogTimeoutMs = 5000; // Default to 5 seconds
MsH3Waitable<bool> g_TestAllDone;

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

struct HeaderValidator {
    struct StoredHeader {
        std::string Name;
        std::string Value;
        StoredHeader(const char* name, size_t nameLen, const char* value, size_t valueLen)
            : Name(name, nameLen), Value(value, valueLen) {}
    };
    
    // Set of all the headers received
    std::vector<StoredHeader> Headers;
    MsH3Waitable<bool> AllHeadersReceived;  // Signal when headers are complete (data received)
    
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
        
        auto ctx = (HeaderValidator*)Context;
        
        if (Event->Type == MSH3_REQUEST_EVENT_HEADER_RECEIVED) {
            const MSH3_HEADER* header = Event->HEADER_RECEIVED.Header;
            
            // Validate the header
            if (!header || !header->Name || header->NameLength == 0 || !header->Value) {
                LOG("Warning: Received invalid header\n");
                return MSH3_STATUS_SUCCESS;
            }
            
            // Save the header data
            ctx->Headers.emplace_back(
                header->Name, header->NameLength, header->Value, header->ValueLength);
            
            LOG("Successfully processed header: %s\n", header->Name);

        } else if (Event->Type == MSH3_REQUEST_EVENT_DATA_RECEIVED) {
            // Signal that all headers have been received (since data always comes after headers)
            LOG("Data received - all headers should be complete\n");
            ctx->AllHeadersReceived.Set(true);

        } else if (Event->Type == MSH3_REQUEST_EVENT_PEER_SEND_SHUTDOWN) {
            // Handle case where request completes without data
            LOG("Peer send shutdown - marking headers as complete\n");
            ctx->AllHeadersReceived.Set(true);

        } else if (Event->Type == MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE) {
            LOG("Request shutdown complete\n");
            // Ensure headers are marked as complete in case other events were missed
            ctx->AllHeadersReceived.Set(true);
        }

        return MSH3_STATUS_SUCCESS;
    }
};

struct TestServer : public MsH3AutoAcceptListener {
    bool SingleThreaded;
    MsH3Configuration Config;
    MsH3Waitable<MsH3Request*> NewRequest;
    TestServer(MsH3Api& Api, bool SingleThread = false)
     : MsH3AutoAcceptListener(Api, MsH3Addr(), ConnectionCallback, this), Config(Api), SingleThreaded(SingleThread) {
        if (Handle && MSH3_FAILED(Config.LoadConfiguration())) {
            MsH3ListenerClose(Handle); Handle = nullptr;
        }
    }
    bool WaitForConnection() noexcept {
        VERIFY(NewConnection.WaitFor());
        auto ServerConnection = NewConnection.Get();
        VERIFY_SUCCESS(ServerConnection->SetConfiguration(Config));
        VERIFY(ServerConnection->Connected.WaitFor());
        return true;
    }
    static
    MSH3_STATUS
    ConnectionCallback(
        MsH3Connection* /* Connection */,
        void* Context,
        MSH3_CONNECTION_EVENT* Event
        ) noexcept {
        auto pThis = (TestServer*)Context;
        if (Event->Type == MSH3_CONNECTION_EVENT_NEW_REQUEST) {
            auto Request = new (std::nothrow) MsH3Request(Event->NEW_REQUEST.Request, CleanUpAutoDelete, MsH3Request::NoOpCallback, pThis);
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

struct TestClient : public MsH3Connection {
    bool SingleThreaded;
    MsH3Configuration Config;
    TestClient(MsH3Api& Api, bool SingleThread = false) : MsH3Connection(Api, CleanUpManual, Callbacks), Config(Api), SingleThreaded(SingleThread) {
        if (Handle && MSH3_FAILED(Config.LoadConfiguration(ClientCredConfig))) {
            MsH3ConnectionClose(Handle); Handle = nullptr;
        }
    }
    MSH3_STATUS Start() noexcept { return MsH3Connection::Start(Config); }
    static
    MSH3_STATUS
    Callbacks(
        MsH3Connection* Connection,
        void* /* Context */,
        MSH3_CONNECTION_EVENT* Event
        ) noexcept {
        if (Event->Type == MSH3_CONNECTION_EVENT_NEW_REQUEST) {
            //
            // Not great beacuse it doesn't provide an application specific
            // error code. If you expect to get streams, you should not no-op
            // the callbacks.
            //
            MsH3RequestClose(Event->NEW_REQUEST.Request);
        } else if (Event->Type == MSH3_CONNECTION_EVENT_CONNECTED) {
            if (((TestClient*)Connection)->SingleThreaded) {
                g_TestAllDone.Set(true);
                Connection->Shutdown();
            }
        } else if (Event->Type == MSH3_CONNECTION_EVENT_SHUTDOWN_COMPLETE) {
            if (((TestClient*)Connection)->SingleThreaded) {
                g_TestAllDone.Set(true);
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
    TestServer Server(Api, true); VERIFY(Server.IsValid());
    TestClient Client(Api, true); VERIFY(Client.IsValid());
    VERIFY_SUCCESS(Client.Start());
    while (!g_TestAllDone.Get()) {
        uint32_t WaitTime = Api.Poll(Execution);
        EventQueue.CompleteEvents(WaitTime);

        auto ServerConnection = Server.NewConnection.GetAndReset();
        if (ServerConnection) {
            VERIFY_SUCCESS(ServerConnection->SetConfiguration(Server.Config));
        }
    }
    return true;
}

DEF_TEST(HandshakeFail) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    VERIFY_SUCCESS(Client.Start());
    VERIFY(!Client.Connected.WaitFor(1500));
    return true;
}

DEF_TEST(HandshakeSetCertTimeout) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.NewConnection.WaitFor());
    VERIFY(!Server.NewConnection.Get()->Connected.WaitFor(1500));
    VERIFY(!Client.Connected.WaitFor());
    Client.Shutdown();
    return true;
}

DEF_TEST(SimpleRequest) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    MsH3Request Request(Client); VERIFY(Request.IsValid());
    VERIFY(Request.Send(RequestHeaders, RequestHeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    VERIFY(Server.NewRequest.WaitFor());
    auto ServerRequest = Server.NewRequest.Get();
    ServerRequest->Shutdown(MSH3_REQUEST_SHUTDOWN_FLAG_GRACEFUL);
    VERIFY(Request.ShutdownComplete.WaitFor());
    //VERIFY(ServerRequest->ShutdownComplete.WaitFor());
    Client.Shutdown();
    return true;
}

bool ReceiveData(bool Async, bool Inline = true) {
    struct TestContext {
        bool Async; bool Inline;
        MsH3Waitable<uint32_t> Data;
        TestContext(bool Async, bool Inline) : Async(Async), Inline(Inline) {}
        static MSH3_STATUS RequestCallback(
            struct MsH3Request* Request,
            void* Context,
            MSH3_REQUEST_EVENT* Event
            ) {
            auto ctx = (TestContext*)Context;
            if (Event->Type == MSH3_REQUEST_EVENT_DATA_RECEIVED) {
                ctx->Data.Set(Event->DATA_RECEIVED.Length);
                if (ctx->Async) {
                    if (ctx->Inline) {
                        Request->CompleteReceive(Event->DATA_RECEIVED.Length);
                    }
                    return MSH3_STATUS_PENDING;
                }
            }
            return MSH3_STATUS_SUCCESS;
        }
    };
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    TestContext Context(Async, Inline);
    MsH3Request Request(Client, MSH3_REQUEST_FLAG_NONE, CleanUpManual, TestContext::RequestCallback, &Context);
    VERIFY(Request.Send(RequestHeaders, RequestHeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(Request.IsValid());
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    VERIFY(Server.NewRequest.WaitFor());
    auto ServerRequest = Server.NewRequest.Get();
    VERIFY(ServerRequest->Send(RequestHeaders, RequestHeadersCount, ResponseData, sizeof(ResponseData), MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(Context.Data.WaitFor());
    VERIFY(Context.Data.Get() == sizeof(ResponseData));
    if (Async && !Inline) {
        Request.CompleteReceive(Context.Data.Get());
    }
    VERIFY(Request.ShutdownComplete.WaitFor());
    //VERIFY(ServerRequest->ShutdownComplete.WaitFor());
    Client.Shutdown();
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
    
    // Create validator on stack instead of heap for better cleanup
    HeaderValidator validator;
    MsH3Request Request(Client, MSH3_REQUEST_FLAG_NONE, CleanUpManual, HeaderValidator::RequestCallback, &validator);
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
    VERIFY(validator.AllHeadersReceived.WaitFor());
    LOG("All headers received\n");
    
    LOG("Header count received: %zu\n", validator.Headers.size());
    VERIFY(validator.Headers.size() == ResponseHeadersCount);
    LOG("Successfully received the expected number of headers\n");
    
    // Verify the header data we copied
    LOG("Verifying header data\n");
    
    // Log how many headers we received
    LOG("Received %zu headers\n", validator.Headers.size());
    for (size_t i = 0; i < validator.Headers.size(); i++) {
        LOG("  Header[%zu]: %s = %s\n", i, 
            validator.Headers[i].Name.c_str(), 
            validator.Headers[i].Value.c_str());
    }
    
    // Check for the status header and verify it
    auto statusHeader = validator.GetHeaderByName(":status", 7);
    VERIFY(statusHeader != nullptr);
    
    // Verify header name
    VERIFY(statusHeader->Name == ":status");
    LOG("Header name verified\n");
    
    // Verify header value
    VERIFY(statusHeader->Value == "200");
    LOG("Header value verified\n");

    // Clean up safely
    LOG("Test complete, shutting down client\n");
    Client.Shutdown();
    VERIFY(Client.ShutdownComplete.WaitFor());
    
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
        HeaderValidator validator;
        
        MsH3Request Request(Client, MSH3_REQUEST_FLAG_NONE, CleanUpManual, HeaderValidator::RequestCallback, &validator);
        
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
        VERIFY(validator.AllHeadersReceived.WaitFor());
        
        // Verify status code
        LOG("Verifying status code\n");
        uint32_t statusCode = validator.GetStatusCode();
        LOG("Status code received: %u\n", statusCode);
        VERIFY(statusCode == 201);
        
        // Verify location header
        auto locationHeader = validator.GetHeaderByName("location", 8);
        VERIFY(locationHeader != nullptr);
        VERIFY(locationHeader->Value == "/resource/123");
        
        LOG("201 Created test passed\n");
    }
    
    // Test 404 Not Found response
    {
        LOG("Testing 404 Not Found response\n");
        // Reset the server's NewRequest waitable
        Server.NewRequest.Reset();
        
        HeaderValidator validator;
        
        MsH3Request Request(Client, MSH3_REQUEST_FLAG_NONE, CleanUpManual, HeaderValidator::RequestCallback, &validator);
        
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
        VERIFY(validator.AllHeadersReceived.WaitFor());
        
        // Verify status code
        uint32_t statusCode = validator.GetStatusCode();
        LOG("Status code received: %u\n", statusCode);
        VERIFY(statusCode == 404);
        
        // Verify content-type header
        auto contentTypeHeader = validator.GetHeaderByName("content-type", 12);
        VERIFY(contentTypeHeader != nullptr);
        VERIFY(contentTypeHeader->Value == "text/plain");
        
        LOG("404 Not Found test passed\n");
    }
    
    // Test 500 Internal Server Error response
    {
        LOG("Testing 500 Internal Server Error response\n");
        // Reset the server's NewRequest waitable
        Server.NewRequest.Reset();
        
        HeaderValidator validator;
        
        MsH3Request Request(Client, MSH3_REQUEST_FLAG_NONE, CleanUpManual, HeaderValidator::RequestCallback, &validator);
        
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
        VERIFY(validator.AllHeadersReceived.WaitFor());
        
        // Verify status code
        uint32_t statusCode = validator.GetStatusCode();
        LOG("Status code received: %u\n", statusCode);
        VERIFY(statusCode == 500);
        
        // Verify content-type header
        auto contentTypeHeader = validator.GetHeaderByName("content-type", 12);
        VERIFY(contentTypeHeader != nullptr);
        VERIFY(contentTypeHeader->Value == "text/plain");
        
        LOG("500 Internal Server Error test passed\n");
    }
    
    // Clean up safely
    LOG("Test complete, shutting down client\n");
    Client.Shutdown();
    VERIFY(Client.ShutdownComplete.WaitFor());
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
    HeaderValidator validator1;
    MsH3Request Request1(Client, MSH3_REQUEST_FLAG_NONE, CleanUpManual, HeaderValidator::RequestCallback, &validator1);
    VERIFY(Request1.Send(RequestHeaders, RequestHeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(Server.NewRequest.WaitFor());
    auto ServerRequest1 = Server.NewRequest.Get();
    LOG("First server request received\n");
    
    VERIFY(ServerRequest1->Send(ResponseHeaders, ResponseHeadersCount, ResponseData, sizeof(ResponseData), MSH3_REQUEST_SEND_FLAG_FIN));
    LOG("First response sent\n");
    
    // Wait for headers and validate status code
    VERIFY(validator1.AllHeadersReceived.WaitFor());
    uint32_t statusCode1 = validator1.GetStatusCode();
    LOG("First status code received: %u\n", statusCode1);
    VERIFY(statusCode1 == 200);
    
    // Verify content-type header
    auto contentType1 = validator1.GetHeaderByName("content-type", 12);
    VERIFY(contentType1 != nullptr);
    VERIFY(contentType1->Value == "application/json");
    LOG("First request headers validated\n");
    
    // Send second request (POST)
    LOG("Sending second request (POST)\n");
    Server.NewRequest.Reset();
    HeaderValidator validator2;
    MsH3Request Request2(Client, MSH3_REQUEST_FLAG_NONE, CleanUpManual, HeaderValidator::RequestCallback, &validator2);
    VERIFY(Request2.Send(PostRequestHeaders, PostRequestHeadersCount, JsonRequestData, sizeof(JsonRequestData) - 1, MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(Server.NewRequest.WaitFor());
    auto ServerRequest2 = Server.NewRequest.Get();
    LOG("Second server request received\n");
    
    VERIFY(ServerRequest2->Send(Response201Headers, Response201HeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
    LOG("Second response sent\n");
    
    // Wait for headers and validate status code
    VERIFY(validator2.AllHeadersReceived.WaitFor());
    uint32_t statusCode2 = validator2.GetStatusCode();
    LOG("Second status code received: %u\n", statusCode2);
    VERIFY(statusCode2 == 201);
    
    // Verify location header
    auto locationHeader = validator2.GetHeaderByName("location", 8);
    VERIFY(locationHeader != nullptr);
    VERIFY(locationHeader->Value == "/resource/123");
    LOG("Second request headers validated\n");
    
    // Send third request (PUT)
    LOG("Sending third request (PUT)\n");
    Server.NewRequest.Reset();
    HeaderValidator validator3;
    MsH3Request Request3(Client, MSH3_REQUEST_FLAG_NONE, CleanUpManual, HeaderValidator::RequestCallback, &validator3);
    VERIFY(Request3.Send(PutRequestHeaders, PutRequestHeadersCount, TextRequestData, sizeof(TextRequestData) - 1, MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(Server.NewRequest.WaitFor());
    auto ServerRequest3 = Server.NewRequest.Get();
    LOG("Third server request received\n");
    
    VERIFY(ServerRequest3->Send(ResponseHeaders, ResponseHeadersCount, ResponseData, sizeof(ResponseData), MSH3_REQUEST_SEND_FLAG_FIN));
    LOG("Third response sent\n");
    
    // Wait for headers and validate status code
    VERIFY(validator3.AllHeadersReceived.WaitFor());
    uint32_t statusCode3 = validator3.GetStatusCode();
    LOG("Third status code received: %u\n", statusCode3);
    VERIFY(statusCode3 == 200);
    
    // Verify content-type header for third request
    auto contentType3 = validator3.GetHeaderByName("content-type", 12);
    VERIFY(contentType3 != nullptr);
    VERIFY(contentType3->Value == "application/json");
    LOG("Third request headers validated\n");
    
    LOG("Test complete, shutting down client\n");
    Client.Shutdown();
    VERIFY(Client.ShutdownComplete.WaitFor());
    return true;
}

// Throughput test sizes
#define LARGE_TEST_SIZE_1MB (1024 * 1024)
#define LARGE_TEST_SIZE_10MB (10 * 1024 * 1024)
#define LARGE_TEST_SIZE_50MB (50 * 1024 * 1024)
#define LARGE_TEST_SIZE_100MB (100 * 1024 * 1024)

struct LargeDataContext {
    size_t TotalReceived = 0;
    size_t ExpectedSize = 0;
    MsH3Waitable<bool> Done;
    LargeDataContext(size_t expected) : ExpectedSize(expected) {}
    static MSH3_STATUS RequestCallback(
        struct MsH3Request* /*Request*/,
        void* Context,
        MSH3_REQUEST_EVENT* Event
    ) {
        auto ctx = (LargeDataContext*)Context;
        if (Event->Type == MSH3_REQUEST_EVENT_DATA_RECEIVED) {
            ctx->TotalReceived += Event->DATA_RECEIVED.Length;
            if (ctx->TotalReceived >= ctx->ExpectedSize) {
                ctx->Done.Set(true);
            }
        }
        if (Event->Type == MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE) {
            ctx->Done.Set(true);
        }
        return MSH3_STATUS_SUCCESS;
    }
};

// Large download (server to client) 1MB
DEF_TEST(LargeDownload1MB) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    LargeDataContext ctx(LARGE_TEST_SIZE_1MB);
    MsH3Request Request(Client, MSH3_REQUEST_FLAG_NONE, CleanUpManual, LargeDataContext::RequestCallback, &ctx);
    VERIFY(Request.Send(RequestHeaders, RequestHeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(Request.IsValid());
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    VERIFY(Server.NewRequest.WaitFor());
    auto ServerRequest = Server.NewRequest.Get();
    std::vector<uint8_t> buffer(LARGE_TEST_SIZE_1MB, 0xAB);
    VERIFY(ServerRequest->Send(ResponseHeaders, ResponseHeadersCount, (const char*)buffer.data(), buffer.size(), MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(ctx.Done.WaitFor());
    VERIFY(ctx.TotalReceived == LARGE_TEST_SIZE_1MB);
    Client.Shutdown();
    VERIFY(Client.ShutdownComplete.WaitFor());
    return true;
}

// Large download (server to client) 10MB
DEF_TEST(LargeDownload10MB) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    LargeDataContext ctx(LARGE_TEST_SIZE_10MB);
    MsH3Request Request(Client, MSH3_REQUEST_FLAG_NONE, CleanUpManual, LargeDataContext::RequestCallback, &ctx);
    VERIFY(Request.Send(RequestHeaders, RequestHeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(Request.IsValid());
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    VERIFY(Server.NewRequest.WaitFor());
    auto ServerRequest = Server.NewRequest.Get();
    std::vector<uint8_t> buffer(LARGE_TEST_SIZE_10MB, 0xCD);
    VERIFY(ServerRequest->Send(ResponseHeaders, ResponseHeadersCount, (const char*)buffer.data(), buffer.size(), MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(ctx.Done.WaitFor());
    VERIFY(ctx.TotalReceived == LARGE_TEST_SIZE_10MB);
    Client.Shutdown();
    VERIFY(Client.ShutdownComplete.WaitFor());
    return true;
}

// Large download (server to client) 50MB
DEF_TEST(LargeDownload50MB) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    LargeDataContext ctx(LARGE_TEST_SIZE_50MB);
    MsH3Request Request(Client, MSH3_REQUEST_FLAG_NONE, CleanUpManual, LargeDataContext::RequestCallback, &ctx);
    VERIFY(Request.Send(RequestHeaders, RequestHeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(Request.IsValid());
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    VERIFY(Server.NewRequest.WaitFor());
    auto ServerRequest = Server.NewRequest.Get();
    std::vector<uint8_t> buffer(LARGE_TEST_SIZE_50MB, 0xA5);
    VERIFY(ServerRequest->Send(ResponseHeaders, ResponseHeadersCount, (const char*)buffer.data(), buffer.size(), MSH3_REQUEST_SEND_FLAG_FIN));
    VERIFY(ctx.Done.WaitFor());
    VERIFY(ctx.TotalReceived == LARGE_TEST_SIZE_50MB);
    Client.Shutdown();
    VERIFY(Client.ShutdownComplete.WaitFor());
    return true;
}

// Large upload (client to server) 1MB
DEF_TEST(LargeUpload1MB) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    MsH3Waitable<size_t> serverReceived;
    struct UploadContext {
        MsH3Waitable<size_t>* Received;
        size_t Total = 0;
        static MSH3_STATUS RequestCallback(
            struct MsH3Request* /*Request*/,
            void* Context,
            MSH3_REQUEST_EVENT* Event
        ) {
            auto ctx = (UploadContext*)Context;
            if (Event->Type == MSH3_REQUEST_EVENT_DATA_RECEIVED) {
                ctx->Total += Event->DATA_RECEIVED.Length;
            }
            if (Event->Type == MSH3_REQUEST_EVENT_PEER_SEND_SHUTDOWN) {
                LOG("Peer send shutdown received, total data length: %zu\n", ctx->Total);
                ctx->Received->Set(ctx->Total);
            }
            return MSH3_STATUS_SUCCESS;
        }
    } uploadCtx{&serverReceived};
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    MsH3Request Request(Client);
    std::vector<uint8_t> buffer(LARGE_TEST_SIZE_1MB, 0xEF);
    VERIFY(Request.Send(RequestHeaders, RequestHeadersCount, buffer.data(), buffer.size(), MSH3_REQUEST_SEND_FLAG_FIN));
    
    LOG("Waiting for server request with timeout\n");
    VERIFY(Server.NewRequest.WaitFor());
    
    auto ServerRequest = Server.NewRequest.Get();
    MsH3RequestSetCallbackHandler(ServerRequest->Handle, (MSH3_REQUEST_CALLBACK_HANDLER)UploadContext::RequestCallback, &uploadCtx);
    VERIFY(ServerRequest->Send(ResponseHeaders, ResponseHeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
    
    LOG("Waiting for server to receive data with timeout\n");
    VERIFY(serverReceived.WaitFor(2000));
    
    VERIFY(serverReceived.Get() == LARGE_TEST_SIZE_1MB);
    Client.Shutdown();
    
    LOG("Waiting for client shutdown with timeout\n");
    VERIFY(Client.ShutdownComplete.WaitFor(1000));
    return true;
}

// Large upload (client to server) 10MB
DEF_TEST(LargeUpload10MB) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    MsH3Waitable<size_t> serverReceived;
    struct UploadContext {
        MsH3Waitable<size_t>* Received;
        size_t Total = 0;
        static MSH3_STATUS RequestCallback(
            struct MsH3Request* /*Request*/,
            void* Context,
            MSH3_REQUEST_EVENT* Event
        ) {
            auto ctx = (UploadContext*)Context;
            if (Event->Type == MSH3_REQUEST_EVENT_DATA_RECEIVED) {
                ctx->Total += Event->DATA_RECEIVED.Length;
            }
            if (Event->Type == MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE) {
                ctx->Received->Set(ctx->Total);
            }
            return MSH3_STATUS_SUCCESS;
        }
    } uploadCtx{&serverReceived};
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    MsH3Request Request(Client);
    std::vector<uint8_t> buffer(LARGE_TEST_SIZE_10MB, 0xBC);
    VERIFY(Request.Send(RequestHeaders, RequestHeadersCount, (const char*)buffer.data(), buffer.size(), MSH3_REQUEST_SEND_FLAG_FIN));
    
    LOG("Waiting for server request with timeout (10MB)\n");
    VERIFY(Server.NewRequest.WaitFor(1000));
    
    auto ServerRequest = Server.NewRequest.Get();
    MsH3RequestSetCallbackHandler(ServerRequest->Handle, (MSH3_REQUEST_CALLBACK_HANDLER)UploadContext::RequestCallback, &uploadCtx);
    VERIFY(ServerRequest->Send(ResponseHeaders, ResponseHeadersCount, nullptr, 0, MSH3_REQUEST_SEND_FLAG_FIN));
    
    LOG("Waiting for server to receive data with timeout (10MB)\n");
    VERIFY(serverReceived.WaitFor(8000)); // Longer timeout for larger data
    
    VERIFY(serverReceived.Get() == LARGE_TEST_SIZE_10MB);
    Client.Shutdown();
    
    LOG("Waiting for client shutdown with timeout (10MB)\n");
    VERIFY(Client.ShutdownComplete.WaitFor(1000));
    return true;
}

// Bidirectional large transfer (client uploads, server responds with large download)
DEF_TEST(LargeBidirectional10MB) {
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    MsH3Waitable<size_t> serverReceived;
    struct UploadContext {
        MsH3Waitable<size_t>* Received;
        size_t Total = 0;
        static MSH3_STATUS RequestCallback(
            struct MsH3Request* /*Request*/,
            void* Context,
            MSH3_REQUEST_EVENT* Event
        ) {
            auto ctx = (UploadContext*)Context;
            if (Event->Type == MSH3_REQUEST_EVENT_DATA_RECEIVED) {
                ctx->Total += Event->DATA_RECEIVED.Length;
            }
            if (Event->Type == MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE) {
                ctx->Received->Set(ctx->Total);
            }
            return MSH3_STATUS_SUCCESS;
        }
    } uploadCtx{&serverReceived};
    LargeDataContext downloadCtx(LARGE_TEST_SIZE_10MB);
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    MsH3Request Request(Client, MSH3_REQUEST_FLAG_NONE, CleanUpManual, LargeDataContext::RequestCallback, &downloadCtx);
    std::vector<uint8_t> uploadBuffer(LARGE_TEST_SIZE_10MB, 0xDE);
    VERIFY(Request.Send(RequestHeaders, RequestHeadersCount, (const char*)uploadBuffer.data(), uploadBuffer.size(), MSH3_REQUEST_SEND_FLAG_FIN));
    
    LOG("Waiting for server request with timeout (bidirectional)\n");
    VERIFY(Server.NewRequest.WaitFor(1000));
    
    auto ServerRequest = Server.NewRequest.Get();
    MsH3RequestSetCallbackHandler(ServerRequest->Handle, (MSH3_REQUEST_CALLBACK_HANDLER)UploadContext::RequestCallback, &uploadCtx);
    std::vector<uint8_t> downloadBuffer(LARGE_TEST_SIZE_10MB, 0xAD);
    VERIFY(ServerRequest->Send(ResponseHeaders, ResponseHeadersCount, (const char*)downloadBuffer.data(), downloadBuffer.size(), MSH3_REQUEST_SEND_FLAG_FIN));
    
    LOG("Waiting for server to receive data with timeout (bidirectional)\n");
    VERIFY(serverReceived.WaitFor(8000)); // Longer timeout for larger data
    
    VERIFY(serverReceived.Get() == LARGE_TEST_SIZE_10MB);
    
    LOG("Waiting for client to receive data with timeout (bidirectional)\n");
    VERIFY(downloadCtx.Done.WaitFor(8000)); // Longer timeout for larger data
    
    VERIFY(downloadCtx.TotalReceived == LARGE_TEST_SIZE_10MB);
    Client.Shutdown();
    
    LOG("Waiting for client shutdown with timeout (bidirectional)\n");
    VERIFY(Client.ShutdownComplete.WaitFor(1000));
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
    ADD_TEST(LargeDownload1MB),
    ADD_TEST(LargeDownload10MB),
    ADD_TEST(LargeDownload50MB),
    ADD_TEST(LargeUpload1MB),
    ADD_TEST(LargeUpload10MB),
    ADD_TEST(LargeBidirectional10MB),
};
const uint32_t TestCount = sizeof(TestFunctions)/sizeof(TestFunc);

// Simple watchdog function: wait for test completion or exit on timeout
void WatchdogFunction() {
    LOG("Watchdog started with timeout %u ms\n", g_WatchdogTimeoutMs);
    if (!g_TestAllDone.WaitFor(g_WatchdogTimeoutMs)) {
        printf("WATCHDOG TIMEOUT!\n"); fflush(stdout);
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
    printf("  --filter=PATTERN   Only run tests matching pattern (supports * wildcard)\n");
    printf("  -v, --verbose       Print detailed test information\n");
    printf("  --help              Print this help message\n");
}

int MSH3_CALL main(int argc, char** argv) {
    // Parse command line options
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_Verbose = true;
        } else if (strncmp(argv[i], "--filter=", 9) == 0) {
            const char* filterValue = argv[i] + 9;
            // If the filter value starts with a quote, remove the quotes
            if (filterValue[0] != '\0' && IsQuoteChar(filterValue[0])) {
                char quoteChar = filterValue[0];
                size_t len = strlen(filterValue);
                // Check if it ends with the same quote
                if (len > 1 && filterValue[len-1] == quoteChar) {
                    // Skip the first quote and null-terminate before the last quote
                    // We're modifying argv directly which is allowed
                    ((char*)filterValue)[len-1] = '\0';
                    filterValue++;
                }
            }
            g_TestFilter = filterValue;
        } else if (strcmp(argv[i], "--help") == 0) {
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
        g_TestAllDone.Set(true);
        
        // Wait for watchdog thread to finish
        if (watchdogThread.joinable()) {
            watchdogThread.join();
        }
        
        LOG("Completed test: %s - %s\n", TestFunctions[i].Name, result ? "PASSED" : "FAILED");

        if (!result) FailCount++;
    }

    printf("Complete! %u test%s failed\n", FailCount, FailCount == 1 ? "" : "s");
    return FailCount ? 1 : 0;
}
