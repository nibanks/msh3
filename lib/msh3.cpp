/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include "msh3.hpp"

const MsQuicApi* MsQuic;
static std::atomic_int MsH3RefCount{0};

//
// Public API
//

extern "C"
void
MSH3_CALL
MsH3Version(
    uint32_t Version[4]
    )
{
    Version[0] = VER_MAJOR;
    Version[1] = VER_MINOR;
    Version[2] = VER_PATCH;
    Version[3] = VER_BUILD_ID;
}

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
    const MSH3_CONNECTION_IF* Interface,
    void* IfContext,
    const char* ServerName,
    uint16_t Port,
    bool Unsecure
    )
{
    auto Reg = (MsQuicRegistration*)Handle;
    auto H3 = new(std::nothrow) MsH3Connection(*Reg, Interface, IfContext, ServerName, Port, Unsecure);
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
    MSH3_CONNECTION* Handle
    )
{
    return ((MsH3Connection*)Handle)->GetState();
}

extern "C"
void
MSH3_CALL
MsH3ConnectionSetCallbackInterface(
    MSH3_CONNECTION* Handle,
    const MSH3_CONNECTION_IF* Interface,
    void* IfContext
    )
{
#ifdef MSH3_SERVER_SUPPORT
    ((MsH3Connection*)Handle)->SetCallbackInterface(Interface, IfContext);
#else
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(IfContext);
#endif
}

extern "C"
void
MSH3_CALL
MsH3ConnectionSetCertificate(
    MSH3_CONNECTION* Handle,
    MSH3_CERTIFICATE* Certificate
    )
{
#ifdef MSH3_SERVER_SUPPORT
    ((MsH3Connection*)Handle)->SetConfiguration(*(MsH3Certificate*)Certificate);
#else
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(Certificate);
#endif
}

extern "C"
MSH3_REQUEST*
MSH3_CALL
MsH3RequestOpen(
    MSH3_CONNECTION* Handle,
    const MSH3_REQUEST_IF* Interface,
    void* IfContext,
    const MSH3_HEADER* Headers,
    size_t HeadersCount,
    MSH3_REQUEST_FLAGS Flags
    )
{
    return (MSH3_REQUEST*)((MsH3Connection*)Handle)->OpenRequest(Interface, IfContext, Headers, HeadersCount, Flags);
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

extern "C"
void
MSH3_CALL
MsH3RequestCompleteReceive(
    MSH3_REQUEST* Handle,
    uint32_t Length
    )
{
    return ((MsH3BiDirStream*)Handle)->CompleteReceive(Length);
}

extern "C"
void
MSH3_CALL
MsH3RequestSetReceiveEnabled(
    MSH3_REQUEST* Handle,
    bool Enabled
    )
{
    return ((MsH3BiDirStream*)Handle)->SetReceiveEnabled(Enabled);
}

extern "C"
bool
MSH3_CALL
MsH3RequestSend(
    MSH3_REQUEST* Handle,
    MSH3_REQUEST_FLAGS Flags,
    const void* Data,
    uint32_t DataLength,
    void* AppContext
    )
{
    return ((MsH3BiDirStream*)Handle)->SendAppData(Flags, Data, DataLength, AppContext);
}

extern "C"
void
MSH3_CALL
MsH3RequestShutdown(
    MSH3_REQUEST* Handle,
    MSH3_REQUEST_SHUTDOWN_FLAGS Flags,
    uint64_t AbortError
    )
{
    (void)((MsH3BiDirStream*)Handle)->Shutdown(AbortError, ToQuicShutdownFlags(Flags));
}

extern "C"
void
MSH3_CALL
MsH3RequestSetCallbackInterface(
    MSH3_REQUEST* Handle,
    const MSH3_REQUEST_IF* Interface,
    void* IfContext
    )
{
#ifdef MSH3_SERVER_SUPPORT
    ((MsH3BiDirStream*)Handle)->SetCallbackInterface(Interface, IfContext);
#else
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(IfContext);
#endif
}

extern "C"
bool
MSH3_CALL
MsH3RequestSendHeaders(
    MSH3_REQUEST* Handle,
    const MSH3_HEADER* Headers,
    size_t HeadersCount,
    MSH3_REQUEST_FLAGS Flags
    )
{
#ifdef MSH3_SERVER_SUPPORT
    return ((MsH3BiDirStream*)Handle)->SendHeaders(Headers, HeadersCount, Flags);
#else
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(Headers);
    UNREFERENCED_PARAMETER(HeadersCount);
    UNREFERENCED_PARAMETER(Flags);
    return false;
#endif
}

extern "C"
MSH3_CERTIFICATE*
MSH3_CALL
MsH3CertificateOpen(
    MSH3_API* Handle,
    const MSH3_CERTIFICATE_CONFIG* Config
    )
{
#ifdef MSH3_SERVER_SUPPORT
    auto Reg = (MsQuicRegistration*)Handle;
    if (Config->Type == MSH3_CERTIFICATE_TYPE_SELF_SIGNED) {
        auto SelfSign = CxPlatGetSelfSignedCert(CXPLAT_SELF_SIGN_CERT_USER, FALSE);
        if (!SelfSign) return nullptr;
        auto Cert = new(std::nothrow) MsH3Certificate(*Reg, SelfSign);
        if (!Cert || QUIC_FAILED(Cert->GetInitStatus())) {
            delete Cert;
            return nullptr;
        }
        return (MSH3_CERTIFICATE*)Cert;
    }
    auto Cert = new(std::nothrow) MsH3Certificate(*Reg, Config);
    if (!Cert || QUIC_FAILED(Cert->GetInitStatus())) {
        delete Cert;
        return nullptr;
    }
    return (MSH3_CERTIFICATE*)Cert;
#else
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(Config);
    return nullptr;
#endif
}

extern "C"
void
MSH3_CALL
MsH3CertificateClose(
    MSH3_CERTIFICATE* Handle
    )
{
#ifdef MSH3_SERVER_SUPPORT
    delete (MsH3Certificate*)Handle;
#else
    UNREFERENCED_PARAMETER(Handle);
#endif
}

extern "C"
MSH3_LISTENER*
MSH3_CALL
MsH3ListenerOpen(
    MSH3_API* Handle,
    const MSH3_ADDR* Address,
    const MSH3_LISTENER_IF* Interface,
    void* IfContext
    )
{
#ifdef MSH3_SERVER_SUPPORT
    auto Reg = (MsQuicRegistration*)Handle;
    auto Listener = new(std::nothrow) MsH3Listener(*Reg,Address, Interface, IfContext);
    if (!Listener || QUIC_FAILED(Listener->GetInitStatus())) {
        delete Listener;
        return nullptr;
    }
    return (MSH3_LISTENER*)Listener;
#else
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(IfContext);
    return nullptr;
#endif
}

extern "C"
void
MSH3_CALL
MsH3ListenerClose(
    MSH3_LISTENER* Handle
    )
{
#ifdef MSH3_SERVER_SUPPORT
    delete (MsH3Listener*)Handle;
#else
    UNREFERENCED_PARAMETER(Handle);
#endif
}

//
// MsH3Connection
//

MsH3Connection::MsH3Connection(
        const MsQuicRegistration& Registration,
        const MSH3_CONNECTION_IF* Interface,
        void* IfContext,
        const char* ServerName,
        uint16_t Port,
        bool Unsecure
    ) : MsQuicConnection(Registration, CleanUpManual, s_MsQuicCallback, this),
        Callbacks(*Interface), Context(IfContext)
{
    if (!IsValid()) return;
    size_t ServerNameLen = strlen(ServerName);
    if (ServerNameLen >= sizeof(HostName)) {
        InitStatus = QUIC_STATUS_OUT_OF_MEMORY;
        return;
    }
    memcpy(HostName, ServerName, ServerNameLen+1);
    lsqpack_enc_preinit(&Encoder, nullptr);
    LocalControl = new(std::nothrow) MsH3UniDirStream(*this, H3StreamTypeControl);
    if (QUIC_FAILED(InitStatus = LocalControl->GetInitStatus())) return;
    LocalEncoder = new(std::nothrow) MsH3UniDirStream(*this, H3StreamTypeEncoder);
    if (QUIC_FAILED(InitStatus = LocalEncoder->GetInitStatus())) return;
    LocalDecoder = new(std::nothrow) MsH3UniDirStream(*this, H3StreamTypeDecoder);
    if (QUIC_FAILED(InitStatus = LocalDecoder->GetInitStatus())) return;

    MsQuicSettings Settings;
    Settings.SetSendBufferingEnabled(false);
    Settings.SetPeerUnidiStreamCount(3);
    Settings.SetIdleTimeoutMs(1000);
    auto Flags =
        Unsecure ?
            QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION :
            QUIC_CREDENTIAL_FLAG_CLIENT;
    MsQuicConfiguration Config(Registration, "h3", Settings, Flags);
    if (QUIC_FAILED(InitStatus = Config.GetInitStatus())) return;
    //if (ServerIp && InitStatus = QUIC_FAILED(H3->SetRemoteAddr(ServerAddress))) return;
    if (QUIC_FAILED(InitStatus = Start(Config, HostName, Port))) return;
}

#ifdef MSH3_SERVER_SUPPORT
MsH3Connection::MsH3Connection(
    HQUIC ServerHandle
    ) : MsQuicConnection(ServerHandle, CleanUpManual, s_MsQuicCallback, this)
{
    lsqpack_enc_preinit(&Encoder, nullptr);
    LocalControl = new(std::nothrow) MsH3UniDirStream(*this, H3StreamTypeControl);
    if (QUIC_FAILED(InitStatus = LocalControl->GetInitStatus())) return;
    LocalEncoder = new(std::nothrow) MsH3UniDirStream(*this, H3StreamTypeEncoder);
    if (QUIC_FAILED(InitStatus = LocalEncoder->GetInitStatus())) return;
    LocalDecoder = new(std::nothrow) MsH3UniDirStream(*this, H3StreamTypeDecoder);
    if (QUIC_FAILED(InitStatus = LocalDecoder->GetInitStatus())) return;
}
#endif // MSH3_SERVER_SUPPORT

MsH3Connection::~MsH3Connection()
{
    lsqpack_enc_cleanup(&Encoder);
    delete LocalDecoder;
    delete LocalEncoder;
    delete LocalControl;
}

MsH3BiDirStream*
MsH3Connection::OpenRequest(
    _In_ const MSH3_REQUEST_IF* Interface,
    _In_ void* IfContext,
    _In_reads_(HeadersCount)
        const MSH3_HEADER* Headers,
    _In_ size_t HeadersCount,
    _In_ MSH3_REQUEST_FLAGS Flags
    )
{
    auto Request = new(std::nothrow) MsH3BiDirStream(*this, Interface, IfContext, Headers, HeadersCount, Flags);
    if (!Request || !Request->IsValid()) {
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
        Callbacks.Connected((MSH3_CONNECTION*)this, Context);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        if (Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status != QUIC_STATUS_CONNECTION_IDLE) {
            //printf("Connection shutdown by transport, 0x%lx\n", (unsigned long)Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
        }
        HandshakeComplete = true;
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        //printf("Connection shutdown by peer, 0x%llx\n", (unsigned long long)Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
        HandshakeComplete = true;
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        SetShutdownComplete();
        Callbacks.ShutdownComplete((MSH3_CONNECTION*)this, Context);
        break;
    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        if (Event->PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL) {
            if (new(std::nothrow) MsH3UniDirStream(*this, Event->PEER_STREAM_STARTED.Stream) == nullptr) {
                MsQuic->StreamClose(Event->PEER_STREAM_STARTED.Stream);
            }
        } else { // Server scenario
#ifdef MSH3_SERVER_SUPPORT
            auto Request = new(std::nothrow) MsH3BiDirStream(*this, Event->PEER_STREAM_STARTED.Stream);
            if (!Request) return QUIC_STATUS_OUT_OF_MEMORY;
            Callbacks.NewRequest((MSH3_CONNECTION*)this, Context, (MSH3_REQUEST*)Request);
#else
            MsQuic->StreamClose(Event->PEER_STREAM_STARTED.Stream);
#endif
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
            printf("Not enough settings.\n");
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

MsH3UniDirStream::MsH3UniDirStream(MsH3Connection& Connection, H3StreamType Type, QUIC_STREAM_OPEN_FLAGS Flags)
    : MsQuicStream(Connection, Flags, CleanUpManual, s_MsQuicCallback, this), H3(Connection), Type(Type)
{
    if (!IsValid()) return;
    Buffer.Buffer[0] = (uint8_t)Type;
    Buffer.Length = 1;
    if (Type == H3StreamTypeControl &&
        !H3WriteSettingsFrame(SettingsH3, ARRAYSIZE(SettingsH3), &Buffer.Length, sizeof(RawBuffer), RawBuffer)) {
        InitStatus = QUIC_STATUS_OUT_OF_MEMORY;
        return;
    }
    InitStatus = Send(&Buffer, 1, QUIC_SEND_FLAG_ALLOW_0_RTT | QUIC_SEND_FLAG_START);
}

MsH3UniDirStream::MsH3UniDirStream(MsH3Connection& Connection, const HQUIC StreamHandle)
    : MsQuicStream(StreamHandle, CleanUpAutoDelete, s_MsQuicCallback, this), H3(Connection), Type(H3StreamTypeUnknown)
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
        printf("Control peer send abort, 0x%llx\n", (unsigned long long)Event->PEER_SEND_ABORTED.ErrorCode);
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        printf("Control peer recv abort, 0x%llx\n", (unsigned long long)Event->PEER_RECEIVE_ABORTED.ErrorCode);
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

void
MsH3UniDirStream::ControlReceive(
    _In_ const QUIC_BUFFER* RecvBuffer
    )
{
    uint32_t Offset = 0;

    do {
        QUIC_VAR_INT FrameType, FrameLength;
        if (!MsH3VarIntDecode(RecvBuffer->Length, RecvBuffer->Buffer, &Offset, &FrameType) ||
            !MsH3VarIntDecode(RecvBuffer->Length, RecvBuffer->Buffer, &Offset, &FrameLength)) {
            printf("Not enough control data yet for frame headers.\n");
            return; // TODO - Implement local buffering
        }

        if (FrameType != H3FrameData && Offset + (uint32_t)FrameLength > RecvBuffer->Length) {
            printf("Not enough control data yet for frame payload.\n");
            return; // TODO - Implement local buffering
        }

        if (FrameType == H3FrameSettings) {
            if (!H3.ReceiveSettingsFrame((uint32_t)FrameLength, RecvBuffer->Buffer + Offset)) return;
        }

        Offset += (uint32_t)FrameLength;

    } while (Offset < RecvBuffer->Length);
}

bool
MsH3UniDirStream::EncodeHeaders(
    _In_ MsH3BiDirStream* Request,
    _In_reads_(HeadersCount)
        const MSH3_HEADER* Headers,
    _In_ size_t HeadersCount
    )
{
    if (lsqpack_enc_start_header(&H3.Encoder, Request->ID(), 0) != 0) {
        printf("lsqpack_enc_start_header failed\n");
        return false;
    }

    size_t enc_off = 0, hea_off = 0;
    for (size_t i = 0; i < HeadersCount; ++i) {
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
        printf("Encoder receive %llu\n", (long long unsigned)Event->RECEIVE.TotalBufferLength);
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        printf("Encoder peer send abort, 0x%llx\n", (long long unsigned)Event->PEER_SEND_ABORTED.ErrorCode);
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        printf("Encoder peer recv abort, 0x%llx\n", (long long unsigned)Event->PEER_RECEIVE_ABORTED.ErrorCode);
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
        printf("Decoder receive %llu\n", (long long unsigned)Event->RECEIVE.TotalBufferLength);
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        printf("Decoder peer send abort, 0x%llx\n", (long long unsigned)Event->PEER_SEND_ABORTED.ErrorCode);
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        printf("Decoder peer recv abort, 0x%llx\n", (long long unsigned)Event->PEER_RECEIVE_ABORTED.ErrorCode);
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
        printf("Unknown peer send abort, 0x%llx\n", (long long unsigned)Event->PEER_SEND_ABORTED.ErrorCode);
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        printf("Unknown peer recv abort, 0x%llx\n", (long long unsigned)Event->PEER_RECEIVE_ABORTED.ErrorCode);
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
    _In_ MsH3Connection& Connection,
    _In_ const MSH3_REQUEST_IF* Interface,
    _In_ void* IfContext,
    _In_reads_(HeadersCount)
        const MSH3_HEADER* Headers,
    _In_ size_t HeadersCount,
    _In_ MSH3_REQUEST_FLAGS Flags
    ) : MsQuicStream(Connection, ToQuicOpenFlags(Flags), CleanUpManual, s_MsQuicCallback, this),
        H3(Connection), Callbacks(*Interface), Context(IfContext)
{
    if (!IsValid()) return;
    InitStatus = QUIC_STATUS_OUT_OF_MEMORY;
    if (!H3.LocalEncoder->EncodeHeaders(this, Headers, HeadersCount)) return;
    auto HeadersLength = Buffers[1].Length + Buffers[2].Length;
    if (!H3WriteFrameHeader(H3FrameHeaders, HeadersLength, &Buffers[0].Length, sizeof(FrameHeaderBuffer), FrameHeaderBuffer)) {
        printf("Framing headers failed\n");
    } else if (QUIC_FAILED(InitStatus = Send(Buffers, 3, ToQuicSendFlags(Flags)))) {
        printf("Headers send failed\n");
    }
}

#ifdef MSH3_SERVER_SUPPORT
MsH3BiDirStream::MsH3BiDirStream(
    _In_ MsH3Connection& Connection,
    _In_ HQUIC StreamHandle
    ) : MsQuicStream(StreamHandle, CleanUpManual, s_MsQuicCallback, this), H3(Connection)
{
}

bool
MsH3BiDirStream::SendHeaders(
    _In_reads_(HeadersCount)
        const MSH3_HEADER* Headers,
    _In_ size_t HeadersCount,
    _In_ MSH3_REQUEST_FLAGS Flags
    )
{
    if (!H3.LocalEncoder->EncodeHeaders(this, Headers, HeadersCount)) {
        printf("Encoding headers failed\n");
        return false;
    }
    auto HeadersLength = Buffers[1].Length + Buffers[2].Length;
    if (!H3WriteFrameHeader(H3FrameHeaders, HeadersLength, &Buffers[0].Length, sizeof(FrameHeaderBuffer), FrameHeaderBuffer)) {
        printf("Framing headers failed\n");
        return false;
    }
    if (QUIC_FAILED(Send(Buffers, 3, ToQuicSendFlags(Flags)))) {
        printf("Headers send failed\n");
        return false;
    }
    return true;
}
#endif

bool
MsH3BiDirStream::SendAppData(
    _In_ MSH3_REQUEST_FLAGS Flags,
    _In_reads_bytes_(DataLength) const void* Data,
    _In_ uint32_t DataLength,
    _In_opt_ void* AppContext
    )
{
    auto AppSend = new(std::nothrow) MsH3AppSend(AppContext); // TODO - Pool alloc
    if (!AppSend || !AppSend->SetData(Data, DataLength) ||
        QUIC_FAILED(Send(AppSend->Buffers, 2, ToQuicSendFlags(Flags), AppSend))) {
        delete AppSend;
        return false;
    }
    return true;
}

QUIC_STATUS
MsH3BiDirStream::MsQuicCallback(
    _Inout_ QUIC_STREAM_EVENT* Event
    )
{
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_START_COMPLETE:
        if (QUIC_FAILED(Event->START_COMPLETE.Status)) {
            if (!Complete) Callbacks.Complete((MSH3_REQUEST*)this, Context, true, 0xffffffffUL);
            Complete = true;
            ShutdownComplete = true;
            Callbacks.ShutdownComplete((MSH3_REQUEST*)this, Context);
        }
        break;
    case QUIC_STREAM_EVENT_RECEIVE:
        CompletedRecvLength = 0;
        PendingRecvLength = Event->RECEIVE.TotalBufferLength;
        for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; ++i) {
            if (!Receive(Event->RECEIVE.Buffers + i)) return QUIC_STATUS_PENDING;
        }
        if (PendingRecvLength < Event->RECEIVE.TotalBufferLength) {
            Event->RECEIVE.TotalBufferLength = CompletedRecvLength;
            ReceiveDisabled = true;
        }
        break;
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        if (Event->SEND_COMPLETE.ClientContext) {
            auto AppSend = (MsH3AppSend*)Event->SEND_COMPLETE.ClientContext;
            Callbacks.DataSent((MSH3_REQUEST*)this, Context, AppSend->AppContext);
            delete AppSend;
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
        if (!Complete) Callbacks.Complete((MSH3_REQUEST*)this, Context, true, 0xffffffffUL);
        if (!ShutdownComplete) Callbacks.ShutdownComplete((MSH3_REQUEST*)this, Context);
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

bool
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
            uint32_t AppReceiveLength = AvailFrameLength;
            if (Callbacks.DataReceived((MSH3_REQUEST*)this, Context, &AppReceiveLength, Buffer->Buffer + Offset)) {
                if (AppReceiveLength < AvailFrameLength) { // Partial receive case

                }
            } else { // Receive pending case
                return false;
            }
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

    CompletedRecvLength += Offset;

    return true;
}

void
MsH3BiDirStream::CompleteReceive(
    _In_ uint32_t Length
    )
{
    UNREFERENCED_PARAMETER(Length); // TODO
}

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
        Header->val_len = (lsxpack_strlen_t)Space;
    } else {
        Header = &CurDecodeHeader;
        lsxpack_header_prepare_decode(Header, DecodeBuffer, 0, Space);
    }
    return Header;
}

void
MsH3BiDirStream::DecodeProcess(
    struct lsxpack_header* Header
    )
{
    const MSH3_HEADER h {
        .Name = Header->buf + Header->name_offset,
        .NameLength = Header->name_len,
        .Value = Header->buf + Header->val_offset,
        .ValueLength = Header->val_len };
    Callbacks.HeaderReceived((MSH3_REQUEST*)this, Context, &h);
}

#ifdef MSH3_SERVER_SUPPORT

//
// MsH3Certificate
//

QUIC_CREDENTIAL_CONFIG ToQuicConfig(const MSH3_CERTIFICATE_CONFIG* Config) {
    QUIC_CREDENTIAL_CONFIG QuicConfig {
        .Type = (QUIC_CREDENTIAL_TYPE)Config->Type,
        .Flags = QUIC_CREDENTIAL_FLAG_NONE,
        .CertificateHash = (QUIC_CERTIFICATE_HASH*)Config->CertificateHash,
        .Principal = nullptr,
        .Reserved = nullptr,
        .AsyncHandler = nullptr,
        .AllowedCipherSuites = QUIC_ALLOWED_CIPHER_SUITE_NONE
    };
    return QuicConfig;
}

MsH3Certificate::MsH3Certificate(
    const MsQuicRegistration& Registration,
    const MSH3_CERTIFICATE_CONFIG* Config
    ) : MsQuicConfiguration(
            Registration,
            "h3",
            MsQuicSettings()
                .SetSendBufferingEnabled(false)
                .SetPeerBidiStreamCount(1000)
                .SetPeerUnidiStreamCount(3)
                .SetIdleTimeoutMs(30000),
            ToQuicConfig(Config))
{
}

MsH3Certificate::MsH3Certificate(
        const MsQuicRegistration& Registration,
        QUIC_CREDENTIAL_CONFIG* SelfSign
    ) : MsQuicConfiguration(
            Registration,
            "h3",
            MsQuicSettings()
                .SetSendBufferingEnabled(false)
                .SetPeerBidiStreamCount(1000)
                .SetPeerUnidiStreamCount(3)
                .SetIdleTimeoutMs(30000),
            *SelfSign),
        SelfSign(SelfSign)
{
}

MsH3Certificate::~MsH3Certificate()
{
    if (SelfSign) CxPlatFreeSelfSignedCert(SelfSign);
}

//
// MsH3Listener
//

MsH3Listener::MsH3Listener(
    const MsQuicRegistration& Registration,
    const MSH3_ADDR* Address,
    const MSH3_LISTENER_IF* Interface,
    void* IfContext
    ) : MsQuicListener(Registration, s_MsQuicCallback, this), Callbacks(*Interface), Context(IfContext)
{
    if (QUIC_SUCCEEDED(InitStatus)) {
        InitStatus = Start("h3", (QUIC_ADDR*)Address);
    }
}

QUIC_STATUS
MsH3Listener::MsQuicCallback(
    _Inout_ QUIC_LISTENER_EVENT* Event
    )
{
    switch (Event->Type) {
    case QUIC_LISTENER_EVENT_NEW_CONNECTION: {
        auto Connection = new(std::nothrow) MsH3Connection(Event->NEW_CONNECTION.Connection);
        if (!Connection) return QUIC_STATUS_OUT_OF_MEMORY;
        Callbacks.NewConnection((MSH3_LISTENER*)this, Context, (MSH3_CONNECTION*)Connection, Event->NEW_CONNECTION.Info->ServerName, Event->NEW_CONNECTION.Info->ServerNameLength);
        break;
    }
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

#endif // MSH3_SERVER_SUPPORT
