/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include <stdio.h>
#include <thread>

#include "msh3.hpp"
#include "msh3.h"

const MsQuicApi* MsQuic;

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
bool
MSH3_API
MsH3Get(
    const char* ServerName,
    const char* Path,
    bool Unsecure
    )
{
    MsQuicRegistration Reg("h3");
    if (QUIC_FAILED(Reg.GetInitStatus())) return false;

    MsQuicSettings Settings;
    Settings.SetSendBufferingEnabled(false);
    Settings.SetPeerBidiStreamCount(1000);
    Settings.SetPeerUnidiStreamCount(3);
    Settings.SetIdleTimeoutMs(5000);
    auto Flags =
        Unsecure ?
            QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION :
            QUIC_CREDENTIAL_FLAG_CLIENT;
    MsQuicConfiguration Config(Reg, MsQuicAlpn("h3", "h3-29"), Settings, MsQuicCredentialConfig(Flags));
    if (QUIC_FAILED(Config.GetInitStatus())) return false;

    MsH3Connection H3(Reg);
    if (QUIC_FAILED(H3.GetInitStatus())) return false;

    //if (ServerIp && QUIC_FAILED(H3.SetRemoteAddr(ServerAddress))) return false;
    if (QUIC_FAILED(H3.Start(Config, ServerName, 443))) return false;

    std::this_thread::sleep_for(std::chrono::seconds(6));

    if (!Path) return false;

    return true;
}

//
// MsH3Connection
//

MsH3Connection::MsH3Connection(const MsQuicRegistration& Registration)
    : MsQuicConnection(Registration, CleanUpManual, s_MsQuicCallback, this)
{
    lsqpack_enc_preinit(&QPack, stderr);
    CreateLocalStreams();
}

MsH3Connection::~MsH3Connection()
{
    lsqpack_enc_cleanup(&QPack);
    delete LocalDecoder;
    delete LocalEncoder;
    delete LocalControl;
}

QUIC_STATUS
MsH3Connection::MsQuicCallback(
    _Inout_ QUIC_CONNECTION_EVENT* Event
    )
{
    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        printf("Connected\n");
        break;
    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        printf("New Peer Stream Flags=%u\n", Event->PEER_STREAM_STARTED.Flags);
        if (Event->PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL) {
            new(std::nothrow) MsH3UniDirStream(this, Event->PEER_STREAM_STARTED.Stream);
        } else {
            MsQuic->StreamClose(Event->PEER_STREAM_STARTED.Stream);
        }
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

const H3Settings SettingsH3[] = {
    { H3SettingQPackMaxTableCapacity, 4096 },
    { H3SettingQPackBlockedStreamsSize, 100 },
};

void MsH3Connection::CreateLocalStreams()
{
    SettingsBuffer.Buffer = RawSettingsBuffer;
    SettingsBuffer.Buffer[0] = H3_STREAM_TYPE_CONTROL;
    SettingsBuffer.Length = 1;
    if (!H3WriteSettingsFrame(SettingsH3, ARRAYSIZE(SettingsH3), &SettingsBuffer.Length, sizeof(RawSettingsBuffer), RawSettingsBuffer)) {
        InitStatus = QUIC_STATUS_OUT_OF_MEMORY;
        return;
    }

    LocalControl = new(std::nothrow) MsH3UniDirStream(this, H3StreamTypeControl);
    if (QUIC_FAILED(InitStatus = LocalControl->GetInitStatus())) return;
    if (QUIC_FAILED(InitStatus = LocalControl->Send(&SettingsBuffer, 1, QUIC_SEND_FLAG_ALLOW_0_RTT | QUIC_SEND_FLAG_START))) return;

    LocalEncoder = new(std::nothrow) MsH3UniDirStream(this, H3StreamTypeEncoder);
    if (QUIC_FAILED(InitStatus = LocalEncoder->GetInitStatus())) return;
    if (QUIC_FAILED(InitStatus = LocalEncoder->Send(&EncoderStreamTypeBuffer, 1, QUIC_SEND_FLAG_ALLOW_0_RTT | QUIC_SEND_FLAG_START))) return;

    LocalDecoder = new(std::nothrow) MsH3UniDirStream(this, H3StreamTypeDecoder);
    if (QUIC_FAILED(InitStatus = LocalDecoder->GetInitStatus())) return;
    if (QUIC_FAILED(InitStatus = LocalDecoder->Send(&DecoderStreamTypeBuffer, 1, QUIC_SEND_FLAG_ALLOW_0_RTT | QUIC_SEND_FLAG_START))) return;
}

//
// MsH3UniDirStream
//

MsH3UniDirStream::MsH3UniDirStream(MsH3Connection* Connection, H3StreamType Type, QUIC_STREAM_OPEN_FLAGS Flags)
    : MsQuicStream(*Connection, Flags, CleanUpManual, s_MsQuicCallback, this), H3(*Connection), Type(Type)
{
}

MsH3UniDirStream::MsH3UniDirStream(MsH3Connection* Connection, const HQUIC StreamHandle)
    : MsQuicStream(StreamHandle, CleanUpAutoDelete, s_MsQuicCallback, this), H3(*Connection), Type(H3StreamTypeUnknown)
{
}

QUIC_STATUS
MsH3UniDirStream::ControlStreamCallback(
    _Inout_ QUIC_STREAM_EVENT* Event
    )
{
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_RECEIVE:
        printf("Control receive\n");
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        printf("Control peer send abort\n");
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        printf("Control peer recv abort\n");
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        printf("Control shutdown complete\n");
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS
MsH3UniDirStream::EncoderStreamCallback(
    _Inout_ QUIC_STREAM_EVENT* Event
    )
{
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_RECEIVE:
        printf("Encoder receive\n");
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        printf("Encoder peer send abort\n");
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        printf("Encoder peer recv abort\n");
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        printf("Encoder shutdown complete\n");
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS
MsH3UniDirStream::DecoderStreamCallback(
    _Inout_ QUIC_STREAM_EVENT* Event
    )
{
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_RECEIVE:
        printf("Decoder receive\n");
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        printf("Decoder peer send abort\n");
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        printf("Decoder peer recv abort\n");
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        printf("Decoder shutdown complete\n");
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS
MsH3UniDirStream::UnknownStreamCallback(
    _Inout_ QUIC_STREAM_EVENT* Event
    )
{
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_RECEIVE:
        printf("Unknown receive %lu\n", Event->RECEIVE.TotalBufferLength);
        if (Event->RECEIVE.TotalBufferLength > 0) {
            auto H3Type = Event->RECEIVE.Buffers[0].Buffer[0];
            switch (H3Type) {
            case H3_STREAM_TYPE_CONTROL:
                printf("New Control stream!\n");
                Type = H3StreamTypeControl;
                H3.PeerControl = this;
                // TODO - Parse rest of the buffer for peer settings
                break;
            case H3_STREAM_TYPE_ENCODER:
                printf("New Encoder stream!\n");
                Type = H3StreamTypeEncoder;
                H3.PeerEncoder = this;
                break;
            case H3_STREAM_TYPE_DECODER:
                printf("New Decoder stream!\n");
                Type = H3StreamTypeDecoder;
                H3.PeerDecoder = this;
                break;
            default:
                printf("Unsupported type %hhu!\n", H3Type);
                break;
            }
        }
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        printf("Unknown peer send abort\n");
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        printf("Unknown peer recv abort\n");
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        printf("Unknown shutdown complete\n");
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}
