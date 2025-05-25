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

// Global flags for command line options
bool g_Verbose = false;
const char* g_TestFilter = nullptr;

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
            TestAllDone = true; \
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
};
const size_t ResponseHeadersCount = sizeof(ResponseHeaders)/sizeof(MSH3_HEADER);

const char ResponseData[] = "HELLO WORLD!\n";

bool TestAllDone = false;

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
                TestAllDone = true;
                Connection->Shutdown();
            }
        } else if (Event->Type == MSH3_CONNECTION_EVENT_SHUTDOWN_COMPLETE) {
            TestAllDone = true;
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
    while (!TestAllDone) {
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

const TestFunc TestFunctions[] = {
    ADD_TEST(Handshake),
    //ADD_TEST(HandshakeSingleThread),
    ADD_TEST(HandshakeFail),
    ADD_TEST(HandshakeSetCertTimeout),
    ADD_TEST(SimpleRequest),
    ADD_TEST(ReceiveDataInline),
    ADD_TEST(ReceiveDataAsync),
    ADD_TEST(ReceiveDataAsyncInline),
};
const uint32_t TestCount = sizeof(TestFunctions)/sizeof(TestFunc);

void PrintUsage(const char* programName) {
    printf("Usage: %s [options]\n", programName);
    printf("Options:\n");
    printf("  --filter=<pattern>  Run only tests matching pattern (wildcards * supported)\n");
    printf("  -v, --verbose       Print detailed test information\n");
    printf("  --help              Print this help message\n");
}

int MSH3_CALL main(int argc, char** argv) {
    // Parse command line options
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_Verbose = true;
        } else if (strncmp(argv[i], "--filter=", 9) == 0) {
            g_TestFilter = argv[i] + 9;
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
        printf("Running %u/%u tests matching filter: '%s'\n", runCount, TestCount, g_TestFilter);
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
        TestAllDone = false;

        LOG("Starting test: %s\n", TestFunctions[i].Name);
        bool result = TestFunctions[i].Func();
        LOG("Completed test: %s - %s\n", TestFunctions[i].Name, result ? "PASSED" : "FAILED");

        if (!result) FailCount++;
    }

    printf("Complete! %u test%s failed\n", FailCount, FailCount == 1 ? "" : "s");
    return FailCount ? 1 : 0;
}
