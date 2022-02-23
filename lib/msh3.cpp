/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include "msh3.hpp"
#include <atomic>

const MsQuicApi* MsQuic;
static std::atomic_int MsH3RefCount{0};

extern "C"
MSH3_API*
MSH3_CALL
MsH3ApiOpen(
    void
    )
{
    if (MsH3RefCount.fetch_add(1) == 0) {
        MsQuic = new(std::nothrow) MsQuicApi();
        if (!MsQuic || QUIC_FAILED(MsQuic->GetInitStatus())) {
            printf("MsQuicApi failed\n");
            delete MsQuic;
            MsQuic = nullptr;
            return nullptr;
        }
    }
    auto Reg = new(std::nothrow) MsQuicRegistration("h3", QUIC_EXECUTION_PROFILE_LOW_LATENCY, true);
    if (!Reg || QUIC_FAILED(Reg->GetInitStatus())) {
        printf("MsQuicRegistration failed\n");
        delete Reg;
        delete MsQuic;
        MsQuic = nullptr;
        return nullptr;
    }
    return (MSH3_API*)Reg;
}

extern "C"
void
MSH3_CALL
MsH3ApiClose(
    MSH3_API* Handle
    )
{
    delete (MsQuicRegistration*)Handle;
    if (MsH3RefCount.fetch_sub(1) == 1) {
        delete MsQuic;
        MsQuic = nullptr;
    }
}

extern "C"
MSH3_CONNECTION*
MSH3_CALL
MsH3ConnectionOpen(
    MSH3_API* Handle,
    const char* ServerName,
    bool Unsecure
    )
{
    auto Reg = (MsQuicRegistration*)Handle;
    auto H3 = new(std::nothrow) MsH3Connection(*Reg, ServerName, 443, Unsecure);
    if (!H3 || QUIC_FAILED(H3->GetInitStatus())) {
        delete H3;
        return nullptr;
    }
    return (MSH3_CONNECTION*)H3;
}

extern "C"
void
MSH3_CALL
MsH3ConnectionClose(
    MSH3_CONNECTION* Handle
    )
{
    auto H3 = (MsH3Connection*)Handle;
    H3->WaitOnShutdownComplete();
    delete H3;
}

extern "C"
MSH3_CONNECTION_STATE
MSH3_CALL
MsH3ConnectionGetState(
    MSH3_CONNECTION* Handle,
    bool WaitForHandshakeComplete
    )
{
    auto H3 = (MsH3Connection*)Handle;
    if (WaitForHandshakeComplete) H3->WaitOnHandshakeComplete();
    return H3->GetState();
}

extern "C"
MSH3_REQUEST*
MSH3_CALL
MsH3RequestOpen(
    MSH3_CONNECTION* Handle,
    const MSH3_REQUEST_IF* Interface,
    void* IfContext,
    const MSH3_HEADER* Headers,
    uint32_t HeadersCount
    )
{
    auto H3 = (MsH3Connection*)Handle;
    if (!H3->WaitOnHandshakeComplete()) return nullptr;
    return (MSH3_REQUEST*)H3->SendRequest(Interface, IfContext, Headers, HeadersCount);
}

extern "C"
void
MSH3_CALL
MsH3RequestClose(
    MSH3_REQUEST* Handle
    )
{
    delete (MsH3BiDirStream*)Handle;
}

//
// MsH3Connection
//

MsH3Connection::MsH3Connection(
        const MsQuicRegistration& Registration,
        const char* ServerName,
        uint16_t Port,
        bool Unsecure
    ) : MsQuicConnection(Registration, CleanUpManual, s_MsQuicCallback, this)
{
    if (!IsValid()) return;
    size_t ServerNameLen = strlen(ServerName);
    if (ServerNameLen >= sizeof(HostName)) {
        InitStatus = QUIC_STATUS_OUT_OF_MEMORY;
        return;
    }
    memcpy(HostName, ServerName, ServerNameLen+1);
    lsqpack_enc_preinit(&Encoder, nullptr);
    LocalControl = new(std::nothrow) MsH3UniDirStream(this, H3StreamTypeControl);
    if (QUIC_FAILED(InitStatus = LocalControl->GetInitStatus())) return;
    LocalEncoder = new(std::nothrow) MsH3UniDirStream(this, H3StreamTypeEncoder);
    if (QUIC_FAILED(InitStatus = LocalEncoder->GetInitStatus())) return;
    LocalDecoder = new(std::nothrow) MsH3UniDirStream(this, H3StreamTypeDecoder);
    if (QUIC_FAILED(InitStatus = LocalDecoder->GetInitStatus())) return;

    MsQuicSettings Settings;
    Settings.SetSendBufferingEnabled(false);
    Settings.SetPeerBidiStreamCount(1000);
    Settings.SetPeerUnidiStreamCount(3);
    Settings.SetIdleTimeoutMs(1000);
    auto Flags =
        Unsecure ?
            QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION :
            QUIC_CREDENTIAL_FLAG_CLIENT;
    MsQuicConfiguration Config(Registration, MsQuicAlpn("h3", "h3-29"), Settings, MsQuicCredentialConfig(Flags));
    if (QUIC_FAILED(InitStatus = Config.GetInitStatus())) return;

    //if (ServerIp && InitStatus = QUIC_FAILED(H3->SetRemoteAddr(ServerAddress))) return;
    if (QUIC_FAILED(InitStatus = Start(Config, HostName, Port))) return;
}

MsH3Connection::~MsH3Connection()
{
    lsqpack_enc_cleanup(&Encoder);
    delete LocalDecoder;
    delete LocalEncoder;
    delete LocalControl;
}

MsH3BiDirStream*
MsH3Connection::SendRequest(
    _In_ const MSH3_REQUEST_IF* Interface,
    _In_ void* IfContext,
    _In_reads_(HeadersCount)
        const MSH3_HEADER* Headers,
    _In_ uint32_t HeadersCount
    )
{
    auto Request = new(std::nothrow) MsH3BiDirStream(this, Interface, IfContext, Headers, HeadersCount);
    if (!Request->IsValid()) {
        delete Request;
        return nullptr;
    }
    return Request;
}

QUIC_STATUS
MsH3Connection::MsQuicCallback(
    _Inout_ QUIC_CONNECTION_EVENT* Event
    )
{
    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        HandshakeSuccess = true;
        SetHandshakeComplete();
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        if (Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status != QUIC_STATUS_CONNECTION_IDLE) {
            printf("Connection shutdown by transport, 0x%x\n", Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
        }
        SetHandshakeComplete();
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        printf("Connection shutdown by peer, 0x%lx\n", Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
        SetHandshakeComplete();
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        SetShutdownComplete();
        break;
    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        if (Event->PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL) {
            if (new(std::nothrow) MsH3UniDirStream(this, Event->PEER_STREAM_STARTED.Stream) == nullptr) {
                MsQuic->StreamClose(Event->PEER_STREAM_STARTED.Stream);
            }
        } else {
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
    uint32_t Offset = 0;

    do {
        QUIC_VAR_INT SettingType, SettingValue;
        if (!MsH3VarIntDecode(BufferLength, Buffer, &Offset, &SettingType) ||
            !MsH3VarIntDecode(BufferLength, Buffer, &Offset, &SettingValue)) {
            printf("Not enough setting.\n");
            return false;
        }

        switch (SettingType) {
        case H3SettingQPackMaxTableCapacity:
            PeerMaxTableSize = (uint32_t)SettingValue;
            break;
        case H3SettingQPackBlockedStreamsSize:
            PeerQPackBlockedStreams = SettingValue;
            break;
        default:
            break;
        }

    } while (Offset < BufferLength);

    tsu_buf_sz = sizeof(tsu_buf);
    if (lsqpack_enc_init(&Encoder, nullptr, 0, 0, 0, LSQPACK_ENC_OPT_STAGE_2, tsu_buf, &tsu_buf_sz) != 0) {
        printf("lsqpack_enc_init failed\n");
        return false;
    }
    lsqpack_dec_init(&Decoder, nullptr, 0, 0, &MsH3BiDirStream::hset_if, (lsqpack_dec_opts)0);

    return true;
}

//
// MsH3UniDirStream
//

MsH3UniDirStream::MsH3UniDirStream(MsH3Connection* Connection, H3StreamType Type, QUIC_STREAM_OPEN_FLAGS Flags)
    : MsQuicStream(*Connection, Flags, CleanUpManual, s_MsQuicCallback, this), H3(*Connection), Type(Type)
{
    if (!IsValid()) return;
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
{ }

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
        printf("Control peer send abort, 0x%lx\n", Event->PEER_SEND_ABORTED.ErrorCode);
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        printf("Control peer recv abort, 0x%lx\n", Event->PEER_RECEIVE_ABORTED.ErrorCode);
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
    uint32_t Offset = 0;

    do {
        QUIC_VAR_INT FrameType, FrameLength;
        if (!MsH3VarIntDecode(Buffer->Length, Buffer->Buffer, &Offset, &FrameType) ||
            !MsH3VarIntDecode(Buffer->Length, Buffer->Buffer, &Offset, &FrameLength)) {
            printf("Not enough control data yet for frame headers.\n");
            return; // TODO - Implement local buffering
        }

        if (FrameType != H3FrameData && Offset + (uint32_t)FrameLength > Buffer->Length) {
            printf("Not enough control data yet for frame payload.\n");
            return; // TODO - Implement local buffering
        }

        if (FrameType == H3FrameSettings) {
            if (!H3.ReceiveSettingsFrame((uint32_t)FrameLength, Buffer->Buffer + Offset)) return;
        }

        Offset += (uint32_t)FrameLength;

    } while (Offset < Buffer->Length);
}

bool 
MsH3UniDirStream::EncodeHeaders(
    _In_ MsH3BiDirStream* Request,
    _In_reads_(HeadersCount)
        const MSH3_HEADER* Headers,
    _In_ uint32_t HeadersCount
    )
{
    if (lsqpack_enc_start_header(&H3.Encoder, Request->ID(), 0) != 0) {
        printf("lsqpack_enc_start_header failed\n");
        return false;
    }

    size_t enc_off = 0, hea_off = 0;
    for (uint32_t i = 0; i < HeadersCount; ++i) {
        H3HeadingPair Header;
        if (!Header.Set(Headers+i)) {
            printf("Header.Set failed\n");
            return false;
        }
        size_t enc_size = sizeof(RawBuffer) - enc_off, hea_size = sizeof(Request->HeadersBuffer) - hea_off;
        auto result = lsqpack_enc_encode(&H3.Encoder, RawBuffer + enc_off, &enc_size, Request->HeadersBuffer + hea_off, &hea_size, &Header, (lsqpack_enc_flags)0);
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
    auto pref_sz = lsqpack_enc_end_header(&H3.Encoder, Request->PrefixBuffer, sizeof(Request->PrefixBuffer), &hflags);
    if (pref_sz < 0) {
        printf("lsqpack_enc_end_header failed\n");
        return false;
    }
    Request->Buffers[1].Length = (uint32_t)pref_sz;

    if (Buffer.Length != 0) {
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
        printf("Encoder peer send abort, 0x%lx\n", Event->PEER_SEND_ABORTED.ErrorCode);
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        printf("Encoder peer recv abort, 0x%lx\n", Event->PEER_RECEIVE_ABORTED.ErrorCode);
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
        printf("Decoder peer send abort, 0x%lx\n", Event->PEER_SEND_ABORTED.ErrorCode);
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        printf("Decoder peer recv abort, 0x%lx\n", Event->PEER_RECEIVE_ABORTED.ErrorCode);
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
            switch (Event->RECEIVE.Buffers[0].Buffer[0]) {
            case H3StreamTypeControl:
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
                Type = H3StreamTypeEncoder;
                H3.PeerEncoder = this;
                break;
            case H3StreamTypeDecoder:
                Type = H3StreamTypeDecoder;
                H3.PeerDecoder = this;
                break;
            default:
                break;
            }
        }
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        printf("Unknown peer send abort, 0x%lx\n", Event->PEER_SEND_ABORTED.ErrorCode);
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        printf("Unknown peer recv abort, 0x%lx\n", Event->PEER_RECEIVE_ABORTED.ErrorCode);
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

//
// MsH3BiDirStream
//

struct lsqpack_dec_hset_if
MsH3BiDirStream::hset_if = {
    .dhi_unblocked      = s_DecodeUnblocked,
    .dhi_prepare_decode = s_DecodePrepare,
    .dhi_process_header = s_DecodeProcess,
};

MsH3BiDirStream::MsH3BiDirStream(
    _In_ MsH3Connection* Connection,
    _In_ const MSH3_REQUEST_IF* Interface,
    _In_ void* IfContext,
    _In_reads_(HeadersCount)
        const MSH3_HEADER* Headers,
    _In_ uint32_t HeadersCount,
    _In_ QUIC_STREAM_OPEN_FLAGS Flags
    ) : MsQuicStream(*Connection, Flags, CleanUpManual, s_MsQuicCallback, this),
        H3(*Connection), Callbacks(*Interface), Context(IfContext)
{
    if (!IsValid()) return;
    InitStatus = QUIC_STATUS_OUT_OF_MEMORY;
    if (QUIC_FAILED(InitStatus = Start())) return;
    InitStatus = QUIC_STATUS_OUT_OF_MEMORY;
    if (!H3.LocalEncoder->EncodeHeaders(this, Headers, HeadersCount)) return;
    auto HeadersLength = Buffers[1].Length+Buffers[2].Length;
    if (HeadersLength != 0) {
        if (!H3WriteFrameHeader(H3FrameHeaders, HeadersLength, &Buffers[0].Length, sizeof(FrameHeaderBuffer), FrameHeaderBuffer)) {
            printf("H3WriteFrameHeader failed\n");
            return;
        }
        if (QUIC_FAILED(InitStatus = Send(Buffers, 3, QUIC_SEND_FLAG_ALLOW_0_RTT | QUIC_SEND_FLAG_FIN))) {
            printf("Request send failed\n");
            return;
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
    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        Complete = true;
        Callbacks.Complete((MSH3_REQUEST*)this, Context, false, 0);
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        Complete = true;
        Callbacks.Complete((MSH3_REQUEST*)this, Context, true, Event->PEER_SEND_ABORTED.ErrorCode);
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        if (!Complete) Callbacks.Complete((MSH3_REQUEST*)this, Context, true, 0);
        Callbacks.Shutdown((MSH3_REQUEST*)this, Context);
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
    uint32_t Offset = 0;

    do {
        if (CurFrameLengthLeft == 0) {
            if (BufferedHeadersLength == 0) {
                if (!MsH3VarIntDecode(Buffer->Length, Buffer->Buffer, &Offset, &CurFrameType) ||
                    !MsH3VarIntDecode(Buffer->Length, Buffer->Buffer, &Offset, &CurFrameLength)) {
                    BufferedHeadersLength = Buffer->Length - Offset;
                    memcpy(BufferedHeaders, Buffer->Buffer + Offset, BufferedHeadersLength);
                    return;
                }
            } else {
                uint32_t ToCopy = sizeof(BufferedHeaders) - BufferedHeadersLength;
                if (ToCopy > Buffer->Length) ToCopy = Buffer->Length;
                memcpy(BufferedHeaders + BufferedHeadersLength, Buffer->Buffer, ToCopy);
                if (!MsH3VarIntDecode(BufferedHeadersLength+ToCopy, BufferedHeaders, &Offset, &CurFrameType) ||
                    !MsH3VarIntDecode(BufferedHeadersLength+ToCopy, BufferedHeaders, &Offset, &CurFrameLength)) {
                    BufferedHeadersLength += ToCopy;
                    return;
                }
                Offset -= BufferedHeadersLength;
                BufferedHeadersLength = 0;
            }
            CurFrameLengthLeft = CurFrameLength;
        }

        uint32_t AvailFrameLength;
        if (Offset + CurFrameLengthLeft > (uint64_t)Buffer->Length) {
            AvailFrameLength = Buffer->Length - Offset; // Rest of the buffer
        } else {
            AvailFrameLength = (uint32_t)CurFrameLengthLeft;
        }

        if (CurFrameType == H3FrameData) {
            Callbacks.DataReceived((MSH3_REQUEST*)this, Context, AvailFrameLength, Buffer->Buffer + Offset);
        } else if (CurFrameType == H3FrameHeaders) {
            const uint8_t* Frame = Buffer->Buffer + Offset;
            if (CurFrameLengthLeft == CurFrameLength) {
                auto rhs =
                    lsqpack_dec_header_in(
                        &H3.Decoder, this, ID(), (size_t)CurFrameLength, &Frame,
                        AvailFrameLength, nullptr, nullptr);
                if (rhs != LQRHS_DONE && rhs != LQRHS_NEED) {
                    printf("lsqpack_dec_header_in failure res=%u\n", rhs);
                }
            } else { // Continued from a previous partial read
                auto rhs =
                    lsqpack_dec_header_read(
                        &H3.Decoder, this, &Frame, AvailFrameLength, nullptr,
                        nullptr);
                if (rhs != LQRHS_DONE && rhs != LQRHS_NEED) {
                    printf("lsqpack_dec_header_read failure res=%u\n", rhs);
                }
            }
        }

        CurFrameLengthLeft -= AvailFrameLength;
        Offset += AvailFrameLength;

    } while (Offset < Buffer->Length);
}

void
MsH3BiDirStream::DecodeUnblocked()
{ }

struct lsxpack_header*
MsH3BiDirStream::DecodePrepare(
    struct lsxpack_header* Header,
    size_t Space
    )
{
    if (Space > sizeof(DecodeBuffer)) {
        printf("Header too big, %zu\n", Space);
        return nullptr;
    }
    if (Header) {
        Header->buf = DecodeBuffer;
        Header->val_len = Space;
    } else {
        Header = &CurDecodeHeader;
        lsxpack_header_prepare_decode(Header, DecodeBuffer, 0, Space);
    }
    return Header;
}

int
MsH3BiDirStream::DecodeProcess(
    struct lsxpack_header* Header
    )
{
    MSH3_HEADER h;
    h.Name = Header->buf + Header->name_offset;
    h.NameLength = Header->name_len;
    h.Value = Header->buf + Header->val_offset;
    h.ValueLength = Header->val_len;
    Callbacks.HeaderReceived((MSH3_REQUEST*)this, Context, &h);
    return 0;
}
