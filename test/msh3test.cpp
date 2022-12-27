/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#define MSH3_SERVER_SUPPORT 1
#define MSH3_TEST_MODE 1

#include "msh3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

using namespace std;

#define VERIFY(X) if (!(X)) { printf(#X " Failed!\n"); exit(1); } else { printf(#X " Succeeded!\n"); }

std::mutex RequestCompleteMutex;
std::condition_variable RequestCompleteEvent;
bool RequestSuccess;

bool WaitOnRequestComplete() {
    if (!RequestSuccess) {
        std::unique_lock Lock{RequestCompleteMutex};
        RequestCompleteEvent.wait(Lock, [&]{return RequestSuccess;});
    }
    return RequestSuccess;
}

void SetRequestComplete() {
    std::lock_guard Lock{RequestCompleteMutex};
    RequestSuccess = true;
    RequestCompleteEvent.notify_all();
}

MSH3_HEADER RequestHeaders[] = {
    { ":method", 7, "GET", 3 },
    { ":path", 5, "/", 1 },
    { ":scheme", 7, "https", 5 },
    { ":authority", 10, "localhost", 9 },
    { "user-agent", 10, "msh3test", 8 },
    { "accept", 6, "*/*", 3 },
};
const size_t RequestHeadersCount = sizeof(RequestHeaders)/sizeof(MSH3_HEADER);

MSH3_HEADER ResponseHeaders[] = {
    { ":status", 7, "200", 3 },
};
const size_t ResponseHeadersCount = sizeof(ResponseHeaders)/sizeof(MSH3_HEADER);

const char ResponseData[] = "HELLO WORLD!\n";

void MSH3_CALL ClientHeaderReceived(MSH3_REQUEST* , void* , const MSH3_HEADER* Header) {
    printf("[res] ");
    fwrite(Header->Name, 1, Header->NameLength, stdout);
    printf(":");
    fwrite(Header->Value, 1, Header->ValueLength, stdout);
    printf("\n");
}

void MSH3_CALL ServerHeaderReceived(MSH3_REQUEST* , void* , const MSH3_HEADER* Header) {
    printf("[req] ");
    fwrite(Header->Name, 1, Header->NameLength, stdout);
    printf(":");
    fwrite(Header->Value, 1, Header->ValueLength, stdout);
    printf("\n");
}

void MSH3_CALL DataReceived(MSH3_REQUEST* , void* , uint32_t Length, const uint8_t* Data) {
    fwrite(Data, 1, Length, stdout);
}

void MSH3_CALL ClientComplete(MSH3_REQUEST* , void* , bool Aborted, uint64_t AbortError) {
    if (Aborted) printf("Response aborted: 0x%llx\n", (long long unsigned)AbortError);
    else         printf("Response complete\n");
    SetRequestComplete();
}

void MSH3_CALL ServerComplete(MSH3_REQUEST* Request, void* , bool Aborted, uint64_t AbortError) {
    if (Aborted) printf("Request aborted: 0x%llx\n", (long long unsigned)AbortError);
    else {
        printf("Request complete\n");
        MsH3RequestSendHeaders(Request, ResponseHeaders, ResponseHeadersCount, MSH3_REQUEST_FLAG_DELAY_SEND);
        MsH3RequestSend(Request, MSH3_REQUEST_FLAG_FIN, ResponseData, sizeof(ResponseData), nullptr);
    }
}

void MSH3_CALL ClientShutdown(MSH3_REQUEST*, void* ) {
    printf("client request shutdown\n");
}

void MSH3_CALL ServerShutdown(MSH3_REQUEST* Request, void* ) {
    printf("server request shutdown\n");
    MsH3RequestClose(Request);
}

void MSH3_CALL DataSent(MSH3_REQUEST* , void* , void* ) {
    printf("Data sent\n");
}

const MSH3_REQUEST_IF ClientRequestIf = { ClientHeaderReceived, DataReceived, ClientComplete, ClientShutdown, DataSent };
const MSH3_REQUEST_IF ServerRequestIf = { ServerHeaderReceived, DataReceived, ServerComplete, ServerShutdown, DataSent };

void MSH3_CALL ConnConnected(MSH3_CONNECTION* , void* ) {
    printf("Connected!\n");
}

void MSH3_CALL ClientConnShutdownComplete(MSH3_CONNECTION* , void* ) {
    printf("client shutdown\n");
}

void MSH3_CALL ServerConnShutdownComplete(MSH3_CONNECTION* Connection, void* ) {
    printf("server shutdown\n");
    MsH3ConnectionClose(Connection);
}

void MSH3_CALL ConnNewRequest(MSH3_CONNECTION* , void* , MSH3_REQUEST* Request) {
    printf("new request\n");
    MsH3RequestSetCallbackInterface(Request, &ServerRequestIf, nullptr);
}

const MSH3_CONNECTION_IF ClientConnIf { ConnConnected, ClientConnShutdownComplete, ConnNewRequest };
const MSH3_CONNECTION_IF ServerConnIf { ConnConnected, ServerConnShutdownComplete, ConnNewRequest };

void MSH3_CALL ListenerNewConnection(MSH3_LISTENER* , void* Context, MSH3_CONNECTION* Connection, const char* , uint16_t) {
    printf("new connection\n");
    auto Cert = (MSH3_CERTIFICATE*)Context;
    MsH3ConnectionSetCallbackInterface(Connection, &ServerConnIf, nullptr);
    printf("set cert\n");
    MsH3ConnectionSetCertificate(Connection, Cert);
}

const MSH3_LISTENER_IF ListenerIf { ListenerNewConnection };

int MSH3_CALL main(int , char**) {
    auto Api = MsH3ApiOpen();
    VERIFY(Api);

    const MSH3_CERTIFICATE_CONFIG Config = { MSH3_CERTIFICATE_TYPE_SELF_SIGNED };
    auto Cert = MsH3CertificateOpen(Api, &Config);
    VERIFY(Cert);

    MSH3_ADDR Address = {0};
#if _WIN32
    Address.Ipv4.sin_port = _byteswap_ushort(4433);
#else
    Address.Ipv4.sin_port = __builtin_bswap16(4433);
#endif
    auto Listener = MsH3ListenerOpen(Api, &Address, &ListenerIf, Cert);
    VERIFY(Listener);

    auto Connection = MsH3ConnectionOpen(Api, &ClientConnIf, nullptr, "localhost", 4433, true);
    VERIFY(Connection);

    auto Request = MsH3RequestOpen(Connection, &ClientRequestIf, nullptr, RequestHeaders, RequestHeadersCount, MSH3_REQUEST_FLAG_FIN);
    VERIFY(Request);

    WaitOnRequestComplete();

    printf("Closing request\n");
    MsH3RequestClose(Request);
    printf("Closing connection\n");
    MsH3ConnectionClose(Connection);
    printf("Closing listener\n");
    MsH3ListenerClose(Listener);
    printf("Closing certificate\n");
    MsH3CertificateClose(Cert);
    printf("Closing api\n");
    MsH3ApiClose(Api);

    return 0;
}
