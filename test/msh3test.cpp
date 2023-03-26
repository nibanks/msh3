/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#define MSH3_TEST_MODE 1
#define MSH3_SERVER_SUPPORT 1

#include "msh3.hpp"
#include <stdio.h>

struct TestFunc {
    bool (*Func)(void);
    const char* Name;
};
#define DEF_TEST(X) bool Test##X()
#define ADD_TEST(X) { Test##X, #X }
#define VERIFY(X) if (!(X)) { printf(#X " Failed!\n"); return false; }

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

DEF_TEST(Handshake) {
    MsH3Api Api; VERIFY(Api.IsValid());
    MsH3Certificate Cert(Api); VERIFY(Cert.IsValid());
    MsH3Listener Listener(Api); VERIFY(Listener.IsValid());
    MsH3Connection Connection(Api); VERIFY(Connection.IsValid());
    VERIFY(Listener.NewConnection.WaitFor());
    auto ServerConnection = Listener.NewConnection.Get();
    ServerConnection->SetCertificate(Cert);
    VERIFY(ServerConnection->Connected.WaitFor());
    VERIFY(Connection.Connected.WaitFor());
    Connection.Shutdown();
    VERIFY(Connection.ShutdownComplete.WaitFor());
    return true;
}

DEF_TEST(HandshakeFail) {
    MsH3Api Api; VERIFY(Api.IsValid());
    MsH3Connection Connection(Api); VERIFY(Connection.IsValid());
    VERIFY(!Connection.Connected.WaitFor(1500));
    return true;
}

DEF_TEST(HandshakeSetCertTimeout) {
    MsH3Api Api; VERIFY(Api.IsValid());
    MsH3Certificate Cert(Api); VERIFY(Cert.IsValid());
    MsH3Listener Listener(Api); VERIFY(Listener.IsValid());
    MsH3Connection Connection(Api); VERIFY(Connection.IsValid());
    VERIFY(Listener.NewConnection.WaitFor());
    auto ServerConnection = Listener.NewConnection.Get();
    //ServerConnection->SetCertificate(Cert);
    VERIFY(!ServerConnection->Connected.WaitFor(1500));
    VERIFY(!Connection.Connected.WaitFor());
    Connection.Shutdown();
    return true;
}

DEF_TEST(SimpleRequest) {
    MsH3Api Api; VERIFY(Api.IsValid());
    MsH3Certificate Cert(Api); VERIFY(Cert.IsValid());
    MsH3Listener Listener(Api); VERIFY(Listener.IsValid());
    MsH3Connection Connection(Api); VERIFY(Connection.IsValid());
    MsH3Request Request(Connection, RequestHeaders, RequestHeadersCount, MSH3_REQUEST_FLAG_FIN); VERIFY(Request.IsValid());
    VERIFY(Listener.NewConnection.WaitFor());
    auto ServerConnection = Listener.NewConnection.Get();
    ServerConnection->SetCertificate(Cert);
    VERIFY(ServerConnection->Connected.WaitFor());
    VERIFY(Connection.Connected.WaitFor());
    VERIFY(ServerConnection->NewRequest.WaitFor());
    auto ServerRequest = ServerConnection->NewRequest.Get();
    ServerRequest->Shutdown(MSH3_REQUEST_SHUTDOWN_FLAG_GRACEFUL);
    VERIFY(Request.Complete.WaitFor());
    VERIFY(ServerRequest->Complete.WaitFor());
    Connection.Shutdown();
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
    MsH3Certificate Cert(Api); VERIFY(Cert.IsValid());
    MsH3Listener Listener(Api); VERIFY(Listener.IsValid());
    MsH3Connection Connection(Api); VERIFY(Connection.IsValid());
    Context Context(Async, Inline);
    MsH3Request Request(Connection, RequestHeaders, RequestHeadersCount, MSH3_REQUEST_FLAG_FIN, &Context, nullptr, Context::RecvData);
    VERIFY(Request.IsValid());
    VERIFY(Listener.NewConnection.WaitFor());
    auto ServerConnection = Listener.NewConnection.Get();
    ServerConnection->SetCertificate(Cert);
    VERIFY(ServerConnection->Connected.WaitFor());
    VERIFY(Connection.Connected.WaitFor());
    VERIFY(ServerConnection->NewRequest.WaitFor());
    auto ServerRequest = ServerConnection->NewRequest.Get();
    VERIFY(ServerRequest->Send(MSH3_REQUEST_FLAG_FIN, ResponseData, sizeof(ResponseData)));
    VERIFY(Context.Data.WaitFor());
    VERIFY(Context.Data.Get() == sizeof(ResponseData));
    if (Async && !Inline) {
        Request.CompleteReceive(Context.Data.Get());
    }
    VERIFY(Request.Complete.WaitFor());
    VERIFY(ServerRequest->Complete.WaitFor());
    Connection.Shutdown();
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
