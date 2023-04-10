/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#define MSH3_TEST_MODE 1

#include "msh3.hpp"
#include <stdio.h>

struct TestFunc {
    bool (*Func)(void);
    const char* Name;
};
#define DEF_TEST(X) bool Test##X()
#define ADD_TEST(X) { Test##X, #X }
#define VERIFY(X) if (!(X)) { printf(#X " Failed on %s:%d!\n", __FILE__, __LINE__); return false; }
#define VERIFY_SUCCESS(X) VERIFY(!MSH3_FAILED(X))

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

struct TestServer : public MsH3Listener {
    MsH3Configuration Config;
    TestServer(MsH3Api& Api) : MsH3Listener(Api), Config(Api) {
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
};

const MSH3_CREDENTIAL_CONFIG ClientCredConfig = {
    MSH3_CREDENTIAL_TYPE_NONE,
    MSH3_CREDENTIAL_FLAG_CLIENT | MSH3_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION,
    nullptr
};

struct TestClient : public MsH3Connection {
    MsH3Configuration Config;
    TestClient(MsH3Api& Api) : MsH3Connection(Api), Config(Api) {
        if (Handle && MSH3_FAILED(Config.LoadConfiguration(ClientCredConfig))) {
            MsH3ConnectionClose(Handle); Handle = nullptr;
        }
    }
    MSH3_STATUS Start() noexcept { return MsH3Connection::Start(Config); }
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
    MsH3Request Request(Client, RequestHeaders, RequestHeadersCount, MSH3_REQUEST_FLAG_FIN); VERIFY(Request.IsValid());
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    auto ServerConnection = Server.NewConnection.Get();
    VERIFY(ServerConnection->NewRequest.WaitFor());
    auto ServerRequest = ServerConnection->NewRequest.Get();
    ServerRequest->Shutdown(MSH3_REQUEST_SHUTDOWN_FLAG_GRACEFUL);
    VERIFY(Request.Complete.WaitFor());
    VERIFY(ServerRequest->Complete.WaitFor());
    Client.Shutdown();
    return true;
}

bool ReceiveData(bool Async, bool Inline = true) {
    struct Context {
        bool Async; bool Inline;
        MsH3Waitable<uint32_t> Data;
        Context(bool Async, bool Inline) : Async(Async), Inline(Inline) {}
        static bool RecvData(MsH3Request* Request, uint32_t* Length, const uint8_t* /* Data */) {
            auto ctx = (Context*)Request->AppContext;
            ctx->Data.Set(*Length);
            if (ctx->Async) {
                if (ctx->Inline) {
                    Request->CompleteReceive(*Length);
                }
                return false;
            }
            return true;
        }
    };
    MsH3Api Api; VERIFY(Api.IsValid());
    TestServer Server(Api); VERIFY(Server.IsValid());
    TestClient Client(Api); VERIFY(Client.IsValid());
    Context Context(Async, Inline);
    MsH3Request Request(Client, RequestHeaders, RequestHeadersCount, MSH3_REQUEST_FLAG_FIN, &Context, nullptr, Context::RecvData);
    VERIFY(Request.IsValid());
    VERIFY_SUCCESS(Client.Start());
    VERIFY(Server.WaitForConnection());
    VERIFY(Client.Connected.WaitFor());
    VERIFY(Server.NewConnection.Get()->NewRequest.WaitFor());
    auto ServerRequest = Server.NewConnection.Get()->NewRequest.Get();
    VERIFY(ServerRequest->Send(MSH3_REQUEST_FLAG_FIN, ResponseData, sizeof(ResponseData)));
    VERIFY(Context.Data.WaitFor());
    VERIFY(Context.Data.Get() == sizeof(ResponseData));
    if (Async && !Inline) {
        Request.CompleteReceive(Context.Data.Get());
    }
    VERIFY(Request.Complete.WaitFor());
    VERIFY(ServerRequest->Complete.WaitFor());
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
    ADD_TEST(HandshakeFail),
    ADD_TEST(HandshakeSetCertTimeout),
    ADD_TEST(SimpleRequest),
    ADD_TEST(ReceiveDataInline),
    ADD_TEST(ReceiveDataAsync),
    ADD_TEST(ReceiveDataAsyncInline),
};
const uint32_t TestCount = sizeof(TestFunctions)/sizeof(TestFunc);

int MSH3_CALL main(int , char**) {
    printf("Running %u tests\n", TestCount);
    uint32_t FailCount = 0;
    for (uint32_t i = 0; i < TestCount; ++i) {
        printf("  %s\n", TestFunctions[i].Name);
        if (!TestFunctions[i].Func()) FailCount++;
    }
    printf("Complete! %u tests failed\n", FailCount);
    return FailCount ? 1 : 0;
}
