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
    if (!H3.SendRequest("get", ServerName, Path)) return false;

    std::this_thread::sleep_for(std::chrono::seconds(6));

    return true;
}

//
// MsH3Connection
//

MsH3Connection::MsH3Connection(const MsQuicRegistration& Registration)
    : MsQuicConnection(Registration, CleanUpManual, s_MsQuicCallback, this)
{
    lsqpack_enc_preinit(&QPack, stderr);
    LocalControl = new(std::nothrow) MsH3UniDirStream(this, H3StreamTypeControl);
    if (QUIC_FAILED(InitStatus = LocalControl->GetInitStatus())) return;
    LocalEncoder = new(std::nothrow) MsH3UniDirStream(this, H3StreamTypeEncoder);
    if (QUIC_FAILED(InitStatus = LocalEncoder->GetInitStatus())) return;
    LocalDecoder = new(std::nothrow) MsH3UniDirStream(this, H3StreamTypeDecoder);
    if (QUIC_FAILED(InitStatus = LocalDecoder->GetInitStatus())) return;
}

MsH3Connection::~MsH3Connection()
{
    lsqpack_enc_cleanup(&QPack);
    for (auto Request : Requests) {
        delete Request;
    }
    delete LocalDecoder;
    delete LocalEncoder;
    delete LocalControl;
}

bool
MsH3Connection::SendRequest(
    _In_z_ const char* Method,
    _In_z_ const char* Host,
    _In_z_ const char* Path
    )
{
    auto Request = new(std::nothrow) MsH3BiDirStream(this, Method, Host, Path);
    if (!Request->IsValid()) {
        delete Request;
        return false;
    }
    return true;
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
        if (Event->PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL) {
            new(std::nothrow) MsH3UniDirStream(this, Event->PEER_STREAM_STARTED.Stream);
        } else {
            printf("Ignoring new peer bidir stream\n");
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

//
// MsH3UniDirStream
//

MsH3UniDirStream::MsH3UniDirStream(MsH3Connection* Connection, H3StreamType Type, QUIC_STREAM_OPEN_FLAGS Flags)
    : MsQuicStream(*Connection, Flags, CleanUpManual, s_MsQuicCallback, this), H3(*Connection), Type(Type)
{
    if (!IsValid()) return;
    Buffer.Buffer = RawBuffer;
    Buffer.Buffer[0] = Type;
    Buffer.Length = 1;
    if (Type == H3StreamTypeControl &&
        !H3WriteSettingsFrame(SettingsH3, ARRAYSIZE(SettingsH3), &Buffer.Length, sizeof(RawBuffer), RawBuffer)) {
        InitStatus = QUIC_STATUS_OUT_OF_MEMORY;
        return;
    }
    InitStatus = Send(&Buffer, 1, QUIC_SEND_FLAG_ALLOW_0_RTT | QUIC_SEND_FLAG_START);
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
        for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; ++i) {
            ControlReceive(Event->RECEIVE.Buffers + i);
        }
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        printf("Control peer send abort\n");
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        printf("Control peer recv abort\n");
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        //printf("Control shutdown complete\n");
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

void
MsH3UniDirStream::ControlReceive(
    _In_ const QUIC_BUFFER* Buffer
    )
{
    //printf("Control receive %u\n", Buffer->Length);

    if (Buffer->Length > UINT16_MAX) {
        printf("TOO BIG BUFFER! NOT SUPPORTED RIGHT NOW!\n");
        return;
    }

    uint16_t Offset = 0;

    do {
        QUIC_VAR_INT FrameType, FrameLength;
        if (!QuicVarIntDecode((uint16_t)Buffer->Length, Buffer->Buffer, &Offset, &FrameType) ||
            !QuicVarIntDecode((uint16_t)Buffer->Length, Buffer->Buffer, &Offset, &FrameLength)) {
            printf("Not enough control data yet for frame headers.\n");
            return; // TODO - Implement local buffering
        }

        if (FrameType != H3FrameData && Offset + FrameLength > (uint64_t)Buffer->Length) {
            printf("Not enough control data yet for frame payload.\n");
            return; // TODO - Implement local buffering
        }

        switch (FrameType) {
        case H3FrameData:
            printf("Received: Data frame len=%lu\n", FrameLength);
            break;
        case H3FrameHeaders:
            printf("Received: Header frame len=%lu\n", FrameLength);
            break;
        case H3FrameSettings:
            if (!ReceiveSettingsFrame((uint32_t)FrameLength, Buffer->Buffer + Offset)) return;
            break;
        default:
            printf("Received: Unknown control frame 0x%lx len=%lu\n", FrameType, FrameLength);
            break;
        }

        Offset += (uint16_t)FrameLength; // TODO - account for overflow

    } while (Offset < (uint16_t)Buffer->Length);
}

bool
MsH3UniDirStream::ReceiveSettingsFrame(
    _In_ uint32_t BufferLength,
    _In_reads_bytes_(BufferLength)
        const uint8_t * const Buffer
    )
{
    uint16_t Offset = 0;

    //printf("Control settings frame len=%u received\n", BufferLength);

    do {
        QUIC_VAR_INT SettingType, SettingValue;
        if (!QuicVarIntDecode((uint16_t)BufferLength, Buffer, &Offset, &SettingType) ||
            !QuicVarIntDecode((uint16_t)BufferLength, Buffer, &Offset, &SettingValue)) {
            printf("Not enough setting.\n");
            return false;
        }

        switch (SettingType) {
        case H3SettingQPackMaxTableCapacity:
            printf("Received: QPackMaxTableCapacity=%lu\n", SettingValue);
            H3.PeerMaxTableSize = (uint32_t)SettingValue;
            break;
        case H3SettingMaxHeaderListSize:
            printf("Received: MaxHeaderListSize=%lu\n", SettingValue);
            break;
        case H3SettingQPackBlockedStreamsSize:
            printf("Received: QPackBlockedStreamsSize=%lu\n", SettingValue);
            H3.PeerQPackBlockedStreams = SettingValue;
            break;
        case H3SettingNumPlaceholders:
            printf("Received: NumPlaceholders=%lu\n", SettingValue);
            break;
        default:
            printf("Received: Unknown setting 0x%lx val=0x%lx\n", SettingType, SettingValue);
            break;
        }

    } while (Offset < (uint16_t)BufferLength);

    if (lsqpack_enc_init(
            &H3.QPack,
            stderr,
            min(H3.PeerMaxTableSize, H3_DEFAULT_QPACK_MAX_TABLE_CAPACITY),
            0,
            0,
            LSQPACK_ENC_OPT_STAGE_2,
            0,
            0) != 0) {
        printf("lsqpack_enc_init failed\n");
        return false;
    }

    return true;
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
        //printf("Encoder shutdown complete\n");
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
        //printf("Decoder shutdown complete\n");
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
        if (Event->RECEIVE.TotalBufferLength > 0) {
            auto H3Type = Event->RECEIVE.Buffers[0].Buffer[0];
            switch (H3Type) {
            case H3StreamTypeControl:
                //printf("New peer control stream!\n");
                Type = H3StreamTypeControl;
                H3.PeerControl = this;
                if (Event->RECEIVE.TotalBufferLength > 1) {
                    auto FirstBuffer = (QUIC_BUFFER*)Event->RECEIVE.Buffers;
                    FirstBuffer->Buffer++; FirstBuffer->Length--;
                    for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; ++i) {
                        ControlReceive(Event->RECEIVE.Buffers + i);
                    }
                }
                break;
            case H3StreamTypeEncoder:
                printf("New peer encoder stream!\n");
                Type = H3StreamTypeEncoder;
                H3.PeerEncoder = this;
                break;
            case H3StreamTypeDecoder:
                printf("New peer decoder stream!\n");
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

//
// MsH3BiDirStream
//

MsH3BiDirStream::MsH3BiDirStream(
    _In_ MsH3Connection* Connection,
    _In_z_ const char* Method,
    _In_z_ const char* Host,
    _In_z_ const char* Path,
    _In_ QUIC_STREAM_OPEN_FLAGS Flags
    ) : MsQuicStream(*Connection, Flags, CleanUpManual, s_MsQuicCallback, this), H3(*Connection)
{
    Headers[0].Name = ":method";
    Headers[0].NameLength = 7;
    Headers[0].Value = Method;
    Headers[0].ValueLength = (uint32_t)strlen(Method);

    Headers[1].Name = ":path";
    Headers[1].NameLength = 5;
    Headers[1].Value = Path;
    Headers[1].ValueLength = (uint32_t)strlen(Path);

    Headers[2].Name = ":scheme";
    Headers[2].NameLength = 7;
    Headers[2].Value = "https";
    Headers[2].ValueLength = 5;

    Headers[3].Name = ":authority";
    Headers[3].NameLength = 10;
    Headers[3].Value = Host;
    Headers[3].ValueLength = (uint32_t)strlen(Host);

    if (!EncodeHeaders()) {
        InitStatus = QUIC_STATUS_OUT_OF_MEMORY;
        return;
    }

    // TODO - Build, send and start

    H3.Requests.push_back(this);
}

bool
MsH3BiDirStream::EncodeHeaders(
    )
{
    return true;
}

QUIC_STATUS
MsH3BiDirStream::MsQuicCallback(
    _Inout_ QUIC_STREAM_EVENT* Event
    )
{
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_RECEIVE:
        printf("Request receive\n");
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        printf("Request peer send abort\n");
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        printf("Request peer recv abort\n");
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        printf("Request shutdown complete\n");
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}
