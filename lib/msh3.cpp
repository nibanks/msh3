/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

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
    if (!H3.WaitOnHandshakeComplete()) return false;
    if (!H3.SendRequest("GET", ServerName, Path)) return false;
    H3.WaitOnShutdownComplete();

    return true;
}

//
// MsH3Connection
//

MsH3Connection::MsH3Connection(const MsQuicRegistration& Registration)
    : MsQuicConnection(Registration, CleanUpManual, s_MsQuicCallback, this)
{
    lsqpack_enc_preinit(&QPack, nullptr);
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
        HandshakeSuccess = true;
        SetHandshakeComplete();
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        printf("Connection shutdown by transport, 0x%x\n", Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
        SetHandshakeComplete();
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        printf("Connection shutdown by peer, 0x%lx\n", Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
        SetHandshakeComplete();
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        //printf("Connection shutdown complete\n");
        SetShutdownComplete();
        break;
    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        if (Event->PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL) {
            new(std::nothrow) MsH3UniDirStream(this, Event->PEER_STREAM_STARTED.Stream);
        } else {
            printf("Ignoring new peer bidir stream\n");
            MsQuic->StreamClose(Event->PEER_STREAM_STARTED.Stream);
        }
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

bool
MsH3Connection::ReceiveSettingsFrame(
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
            PeerMaxTableSize = (uint32_t)SettingValue;
            break;
        case H3SettingMaxHeaderListSize:
            printf("Received: MaxHeaderListSize=%lu\n", SettingValue);
            break;
        case H3SettingQPackBlockedStreamsSize:
            printf("Received: QPackBlockedStreamsSize=%lu\n", SettingValue);
            PeerQPackBlockedStreams = SettingValue;
            break;
        case H3SettingNumPlaceholders:
            printf("Received: NumPlaceholders=%lu\n", SettingValue);
            break;
        default:
            printf("Received: Unknown setting 0x%lx val=0x%lx\n", SettingType, SettingValue);
            break;
        }

    } while (Offset < (uint16_t)BufferLength);

    tsu_buf_sz = sizeof(tsu_buf);
    if (lsqpack_enc_init(
            &QPack,
            nullptr,
            min(PeerMaxTableSize, H3_DEFAULT_QPACK_MAX_TABLE_CAPACITY),
            0,
            0,
            LSQPACK_ENC_OPT_STAGE_2,
            tsu_buf,
            &tsu_buf_sz) != 0) {
        printf("lsqpack_enc_init failed\n");
        return false;
    }

    return true;
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
            if (!H3.ReceiveSettingsFrame((uint32_t)FrameLength, Buffer->Buffer + Offset)) return;
            break;
        default:
            printf("Received: Unknown control frame 0x%lx len=%lu\n", FrameType, FrameLength);
            break;
        }

        Offset += (uint16_t)FrameLength; // TODO - account for overflow

    } while (Offset < (uint16_t)Buffer->Length);
}

bool MsH3UniDirStream::EncodeHeaders(_In_ MsH3BiDirStream* Request)
{
    if (lsqpack_enc_start_header(&H3.QPack, Request->ID(), 0) != 0) {
        printf("lsqpack_enc_start_header failed\n");
        return false;
    }

    size_t enc_off = 0, hea_off = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        size_t enc_size = sizeof(RawBuffer) - enc_off, hea_size = sizeof(Request->HeadersBuffer) - hea_off;
        auto result = lsqpack_enc_encode(&H3.QPack, RawBuffer + enc_off, &enc_size, Request->HeadersBuffer + hea_off, &hea_size, &Request->Headers[i], (lsqpack_enc_flags)0);
        if (result != LQES_OK) {
            printf("lsqpack_enc_encode failed, %d\n", result);
            return false;
        }
        enc_off += enc_size;
        hea_off += hea_size;
    }

    Buffer.Length = (uint32_t)enc_off;
    Request->Buffers[2].Length = (uint32_t)hea_off;

    enum lsqpack_enc_header_flags hflags;
    auto pref_sz = lsqpack_enc_end_header(&H3.QPack, Request->PrefixBuffer, sizeof(Request->PrefixBuffer), &hflags);
    if (pref_sz < 0) {
        printf("lsqpack_enc_end_header failed\n");
        return false;
    }

    Request->Buffers[1].Length = (uint32_t)pref_sz;

    if (Buffer.Length != 0) {
        printf("Send: Encoder len=%u\n", Buffer.Length);
        if (QUIC_FAILED(Send(&Buffer, 1, QUIC_SEND_FLAG_ALLOW_0_RTT))) {
            printf("Encoder send failed\n");
        }
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
        printf("Encoder receive %lu\n", Event->RECEIVE.TotalBufferLength);
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
        printf("Decoder receive %lu\n", Event->RECEIVE.TotalBufferLength);
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
                //printf("New peer encoder stream!\n");
                Type = H3StreamTypeEncoder;
                H3.PeerEncoder = this;
                break;
            case H3StreamTypeDecoder:
                //printf("New peer decoder stream!\n");
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
    InitStatus = QUIC_STATUS_OUT_OF_MEMORY;
    if (!Headers[0].Set(":method", Method)) return;
    if (!Headers[1].Set(":path", Path)) return;
    if (!Headers[2].Set(":scheme", "http")) return;
    if (!Headers[3].Set(":authority", Host)) return;
    if (QUIC_FAILED(InitStatus = Start())) return;
    if (!H3.LocalEncoder->EncodeHeaders(this)) return;
    H3.Requests.push_back(this);
    auto HeadersLength = Buffers[1].Length+Buffers[2].Length;
    if (HeadersLength != 0) {
        printf("Request: Send header len=%u\n", HeadersLength);
        if (!H3WriteFrameHeader(H3FrameHeaders, HeadersLength, &Buffers[0].Length, sizeof(FrameHeaderBuffer), FrameHeaderBuffer)) {
            printf("H3WriteFrameHeader failed\n");
            return;
        }
        if (QUIC_FAILED(Send(Buffers, 3, QUIC_SEND_FLAG_ALLOW_0_RTT | QUIC_SEND_FLAG_FIN))) {
            printf("Request send failed\n");
        }
    }
    InitStatus = QUIC_STATUS_SUCCESS;
}

QUIC_STATUS
MsH3BiDirStream::MsQuicCallback(
    _Inout_ QUIC_STREAM_EVENT* Event
    )
{
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_RECEIVE:
        for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; ++i) {
            Receive(Event->RECEIVE.Buffers + i);
        }
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        printf("Request peer send abort\n");
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        printf("Request peer recv abort\n");
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        //printf("Request shutdown complete\n");
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

void
MsH3BiDirStream::Receive(
    _In_ const QUIC_BUFFER* Buffer
    )
{
    //printf("Request receive %u\n", Buffer->Length);

    /*if (Buffer->Length > UINT16_MAX) {
        printf("TOO BIG BUFFER! NOT SUPPORTED RIGHT NOW!\n");
        return;
    }*/

    uint16_t Offset = 0;

    do {
        QUIC_VAR_INT FrameType, FrameLength;
        if (CurFrameType == H3FrameUnknown) {
            if (!QuicVarIntDecode((uint16_t)Buffer->Length, Buffer->Buffer, &Offset, &FrameType) ||
                !QuicVarIntDecode((uint16_t)Buffer->Length, Buffer->Buffer, &Offset, &FrameLength)) {
                printf("Not enough request data yet for frame headers.\n");
                return; // TODO - Implement local buffering
            }
        } else {
            FrameLength = CurFrameLength;
            FrameType = CurFrameType;
            CurFrameType = H3FrameUnknown;
        }

        if (FrameType != H3FrameData && Offset + FrameLength > (uint64_t)Buffer->Length) {
            printf("Not enough request data yet for frame %lu payload.\n", FrameType);
            CurFrameType = (H3FrameType)FrameType;
            CurFrameLength = (Offset + FrameLength) - Buffer->Length;
            return; // TODO - Implement local buffering
        }

        switch (FrameType) {
        case H3FrameData:
            //printf("Received: Data frame len=%lu\n", FrameLength);
            for (uint32_t i = 0; i < (uint32_t)FrameLength; ++i) {
                printf("%c", Buffer->Buffer[Offset + i]);
            }
            printf("\n");
            if (Offset + FrameLength > (uint64_t)Buffer->Length) {
                CurFrameType = H3FrameData;
                CurFrameLength = (Offset + FrameLength) - Buffer->Length;
            }
            break;
        case H3FrameHeaders:
            printf("Received: Header frame len=%lu\n", FrameLength);
            break;
        default:
            printf("Received: Unknown request frame 0x%lx len=%lu\n", FrameType, FrameLength);
            break;
        }

        Offset += (uint16_t)FrameLength; // TODO - account for overflow

    } while (Offset < (uint16_t)Buffer->Length);
}
