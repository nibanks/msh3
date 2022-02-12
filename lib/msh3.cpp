/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include <msquic.hpp>
#include "../msh3.h" // TODO - Fix include path so relative path isn't necessary

const MsQuicApi* MsQuic;

struct MsH3Connection : public MsQuicConnection {

    MsH3Connection(const MsQuicRegistration& Registration)
        : MsQuicConnection(Registration, CleanUpManual, s_MsQuicCallback, this)
    { }

    static
    QUIC_STATUS
    s_MsQuicCallback(
        _In_ MsQuicConnection* /* Connection */,
        _In_opt_ void* Context,
        _Inout_ QUIC_CONNECTION_EVENT* Event
        )
    {
        return ((MsH3Connection*)Context)->MsQuicCallback(Event);
    }

    QUIC_STATUS
    MsQuicCallback(
        _Inout_ QUIC_CONNECTION_EVENT* Event
        )
    {
        switch (Event->Type) {
        case QUIC_CONNECTION_EVENT_CONNECTED:
            printf("Connected\n");
            break;
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
            printf("New Peer Stream Flags=%u\n", Event->PEER_STREAM_STARTED.Flags);
            //NewPeerStream(H3, Event->PEER_STREAM_STARTED.Stream, Event->PEER_STREAM_STARTED.Flags);
            break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
            printf("Connection shutdown complete\n");
            //H3->Shutdown.Set();
            break;
        default:
            break;
        }
        return QUIC_STATUS_SUCCESS;
    }
};

extern "C"
bool
MSH3_API
MsH3Open(
    void
    )
{
    if (!MsQuic) {
        MsQuic = new(std::nothrow) MsQuicApi();
        if (QUIC_FAILED(MsQuic->GetInitStatus())) {
            return false;
        }
    }
    return true;
}

extern "C"
void
MSH3_API
MsH3Close(
    void
    )
{
    delete MsQuic;
    MsQuic = nullptr;
}

extern "C"
void
MSH3_API
MsH3Get(
    const char* ServerName,
    const char* Path,
    bool Unsecure
    )
{
    MsQuicRegistration Reg("h3");
    if (QUIC_FAILED(Reg.GetInitStatus())) return;

    MsQuicAlpn Alpns("h3", "h3-29");
    MsQuicSettings Settings;
    Settings.SetSendBufferingEnabled(false);
    Settings.SetPeerBidiStreamCount(1000);
    Settings.SetPeerUnidiStreamCount(3);
    Settings.SetIdleTimeoutMs(5000);
    auto Flags =
        Unsecure ?
            QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION :
            QUIC_CREDENTIAL_FLAG_CLIENT;
    MsQuicCredentialConfig Creds(Flags);
    MsQuicConfiguration Config(Reg, Alpns, Settings, Creds);
    if (QUIC_FAILED(Config.GetInitStatus())) return;

    MsH3Connection H3(Reg);
    if (QUIC_FAILED(H3.GetInitStatus())) return;

    //if (ServerIp) ASSERT_SUCCESS(H3.SetRemoteAddr(ServerAddress));
    if (QUIC_FAILED(H3.Start(Config, ServerName, 443))) return;
}
