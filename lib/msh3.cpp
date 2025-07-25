/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#define MSH3_API_ENABLE_PREVIEW_FEATURES 1  // Always enable preview features for now
#define QUIC_API_ENABLE_PREVIEW_FEATURES 1  // Always enable preview features for now

#include "msh3_internal.hpp"

const MsQuicApi* MsQuic;
QUIC_EXECUTION** MsQuicExecutions;
uint32_t MsQuicExecutionCount;
static std::atomic_int MsH3pRefCount{0};

#if MSH3_DEBUG_IO
void DebugIoBuffer(
    const QUIC_BUFFER* Buffer,
    const char* Prefix,
    uint32_t Type
    )
{
    printf("uni[%u] %s: ", Type, Prefix);
    for (uint32_t j = 0; j < Buffer->Length; ++j) {
        if (j > 0 && j % 16 == 0) {
            printf("\n             ");
        }
        printf("%02x ", ((uint8_t*)Buffer->Buffer)[j]);
    }
    printf("\n");
}
#else
#define DebugIoBuffer(Buffer, Prefix, Type) ((void)0)
#endif // MSH3_DEBUG_IO

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
    if (MsH3pRefCount.fetch_add(1) == 0) {
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
MSH3_API*
MSH3_CALL
MsH3ApiOpenWithExecution(
    uint32_t ExecutionConfigCount,
    MSH3_EXECUTION_CONFIG* ExecutionConfigs,
    MSH3_EXECUTION** Executions
    )
{
    if (ExecutionConfigCount == 0 || ExecutionConfigs == nullptr || Executions == nullptr) {
        return nullptr;
    }
    if (MsH3pRefCount.fetch_add(1) == 0) {
        MsQuic = new(std::nothrow) MsQuicApi();
        if (!MsQuic || QUIC_FAILED(MsQuic->GetInitStatus())) {
            printf("MsQuicApi failed\n");
            delete MsQuic;
            MsQuic = nullptr;
            return nullptr;
        }
        auto Status =
            MsQuic->ExecutionCreate(
                QUIC_GLOBAL_EXECUTION_CONFIG_FLAG_NONE,
                0,
                ExecutionConfigCount,
                (QUIC_EXECUTION_CONFIG*)ExecutionConfigs,
                (QUIC_EXECUTION**)Executions);
        if (QUIC_FAILED(Status)) {
            printf("MsQuicExecutionCreate failed\n");
            delete MsQuic;
            MsQuic = nullptr;
            return nullptr;
        }
        MsQuicExecutions = (QUIC_EXECUTION**)Executions;
        MsQuicExecutionCount = ExecutionConfigCount;
    } else {
        return nullptr; // Already opened
    }
    auto Reg = new(std::nothrow) MsQuicRegistration("h3", QUIC_EXECUTION_PROFILE_LOW_LATENCY, true);
    if (!Reg || QUIC_FAILED(Reg->GetInitStatus())) {
        printf("MsQuicRegistration failed\n");
        delete Reg;
        MsQuic->ExecutionDelete(MsQuicExecutionCount, MsQuicExecutions);
        MsQuicExecutions = nullptr;
        MsQuicExecutionCount = 0;
        delete MsQuic;
        MsQuic = nullptr;
        return nullptr;
    }
    return (MSH3_API*)Reg;
}

extern "C"
uint32_t
MSH3_CALL
MsH3ApiPoll(
    _In_ MSH3_EXECUTION* Execution
    )
{
    return MsQuic->ExecutionPoll((QUIC_EXECUTION*)Execution);
}

extern "C"
void
MSH3_CALL
MsH3ApiClose(
    MSH3_API* Handle
    )
{
    auto Reg = (MsQuicRegistration*)Handle;
    Reg->Shutdown(QUIC_CONNECTION_SHUTDOWN_FLAG_SILENT, 0);
    delete Reg;
    if (MsH3pRefCount.fetch_sub(1) == 1) {
        delete MsQuic;
        MsQuic = nullptr;
        MsQuicExecutions = nullptr;
        MsQuicExecutionCount = 0;
    }
}

extern "C"
MSH3_CONFIGURATION*
MSH3_CALL
MsH3ConfigurationOpen(
    MSH3_API* Handle,
    const MSH3_SETTINGS* Settings, // optional
    uint32_t SettingsLength
    )
{
    auto Reg = (MsQuicRegistration*)Handle;
    auto Configuration = new(std::nothrow) MsH3pConfiguration(*Reg, Settings, SettingsLength);
    if (!Configuration || QUIC_FAILED(Configuration->GetInitStatus())) {
        delete Configuration;
        return nullptr;
    }
    return (MSH3_CONFIGURATION*)Configuration;
}

extern "C"
MSH3_STATUS
MSH3_CALL
MsH3ConfigurationLoadCredential(
    MSH3_CONFIGURATION* Handle,
    const MSH3_CREDENTIAL_CONFIG* CredentialConfig
    )
{
    auto Configuration = (MsH3pConfiguration*)Handle;
    return Configuration->LoadH3Credential(CredentialConfig);
}

extern "C"
void
MSH3_CALL
MsH3ConfigurationClose(
    MSH3_CONFIGURATION* Handle
    )
{
    delete (MsH3pConfiguration*)Handle;
}

extern "C"
MSH3_CONNECTION*
MSH3_CALL
MsH3ConnectionOpen(
    MSH3_API* Handle,
    const MSH3_CONNECTION_CALLBACK_HANDLER Handler,
    void* Context
    )
{
    auto Reg = (MsQuicRegistration*)Handle;
    auto H3 = new(std::nothrow) MsH3pConnection(*Reg, Handler, Context);
    if (!H3 || QUIC_FAILED(H3->GetInitStatus())) {
        delete H3;
        return nullptr;
    }
    return (MSH3_CONNECTION*)H3;
}

extern "C"
void
MSH3_CALL
MsH3ConnectionSetCallbackHandler(
    MSH3_CONNECTION* Handle,
    const MSH3_CONNECTION_CALLBACK_HANDLER Handler,
    void* Context
    )
{
    ((MsH3pConnection*)Handle)->SetCallbackHandler(Handler, Context);
}

extern "C"
MSH3_STATUS
MSH3_CALL
MsH3ConnectionSetConfiguration(
    MSH3_CONNECTION* Handle,
    MSH3_CONFIGURATION* Configuration
    )
{
    return ((MsH3pConnection*)Handle)->SetConfigurationH3(*(MsH3pConfiguration*)Configuration);
}

extern "C"
MSH3_STATUS
MSH3_CALL
MsH3ConnectionStart(
    MSH3_CONNECTION* Handle,
    MSH3_CONFIGURATION* Configuration,
    const char* ServerName,
    const MSH3_ADDR* ServerAddress
    )
{
    return ((MsH3pConnection*)Handle)->StartH3(*(MsH3pConfiguration*)Configuration, ServerName, ServerAddress);
}

extern "C"
void
MSH3_CALL
MsH3ConnectionShutdown(
    MSH3_CONNECTION* Handle,
    uint64_t ErrorCode
    )
{
    ((MsH3pConnection*)Handle)->Shutdown(ErrorCode);
}

extern "C"
void
MSH3_CALL
MsH3ConnectionClose(
    MSH3_CONNECTION* Handle
    )
{
    delete (MsH3pConnection*)Handle;
}

extern "C"
MSH3_STATUS
MSH3_CALL
MsH3ConnectionGetQuicParam(
    MSH3_CONNECTION* Handle,
    uint32_t Param,
    uint32_t* BufferLength,
    void* Buffer
    )
{
    if (!Handle || !BufferLength) {
        return MSH3_STATUS_INVALID_STATE;
    }
    return MsQuic->GetParam(((MsH3pConnection*)Handle)->Handle, Param, BufferLength, Buffer);
}

extern "C"
MSH3_REQUEST*
MSH3_CALL
MsH3RequestOpen(
    MSH3_CONNECTION* Handle,
    const MSH3_REQUEST_CALLBACK_HANDLER Handler,
    void* Context,
    MSH3_REQUEST_FLAGS Flags
    )
{
    auto Request = new(std::nothrow) MsH3pBiDirStream(*(MsH3pConnection*)Handle, Handler, Context, Flags);
    if (!Request || !Request->IsValid()) {
        delete Request;
        return nullptr;
    }
    return (MSH3_REQUEST*)Request;
}

extern "C"
void
MSH3_CALL
MsH3RequestClose(
    MSH3_REQUEST* Handle
    )
{
    delete (MsH3pBiDirStream*)Handle;
}

extern "C"
MSH3_STATUS
MSH3_CALL
MsH3RequestGetQuicParam(
    MSH3_REQUEST* Handle,
    uint32_t Param,
    uint32_t* BufferLength,
    void* Buffer
    )
{
    if (!Handle || !BufferLength) {
        return MSH3_STATUS_INVALID_STATE;
    }
    return MsQuic->GetParam(((MsH3pBiDirStream*)Handle)->Handle, Param, BufferLength, Buffer);
}

extern "C"
void
MSH3_CALL
MsH3RequestCompleteReceive(
    MSH3_REQUEST* Handle,
    uint32_t Length
    )
{
    ((MsH3pBiDirStream*)Handle)->CompleteReceive(Length);
}

extern "C"
void
MSH3_CALL
MsH3RequestSetReceiveEnabled(
    MSH3_REQUEST* Handle,
    bool Enabled
    )
{
    (void)((MsH3pBiDirStream*)Handle)->ReceiveSetEnabled(Enabled);
}

extern "C"
bool
MSH3_CALL
MsH3RequestSend(
    MSH3_REQUEST* Handle,
    MSH3_REQUEST_SEND_FLAGS Flags,
    const MSH3_HEADER* Headers,
    size_t HeadersCount,
    const void* Data,
    uint32_t DataLength,
    void* AppContext
    )
{
    return ((MsH3pBiDirStream*)Handle)->Send(Flags, Headers, HeadersCount, Data, DataLength, AppContext);
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
    (void)((MsH3pBiDirStream*)Handle)->Shutdown(AbortError, ToQuicShutdownFlags(Flags));
}

extern "C"
void
MSH3_CALL
MsH3RequestSetCallbackHandler(
    MSH3_REQUEST* Handle,
    const MSH3_REQUEST_CALLBACK_HANDLER Handler,
    void* Context
    )
{
    ((MsH3pBiDirStream*)Handle)->SetCallbackHandler(Handler, Context);
}

extern "C"
MSH3_LISTENER*
MSH3_CALL
MsH3ListenerOpen(
    MSH3_API* Handle,
    const MSH3_ADDR* Address,
    const MSH3_LISTENER_CALLBACK_HANDLER Handler,
    void* Context
    )
{
    auto Reg = (MsQuicRegistration*)Handle;
    auto Listener = new(std::nothrow) MsH3pListener(*Reg,Address, Handler, Context);
    if (!Listener || QUIC_FAILED(Listener->GetInitStatus())) {
        delete Listener;
        return nullptr;
    }
    return (MSH3_LISTENER*)Listener;
}

extern "C"
void
MSH3_CALL
MsH3ListenerClose(
    MSH3_LISTENER* Handle
    )
{
    delete (MsH3pListener*)Handle;
}

//
// MsH3pConfiguration
//

struct MsH3pSettings : public MsQuicSettings {
    MsQuicSettings& SetKeepAliveIntervalMs(uint32_t Value) { KeepAliveIntervalMs = Value; IsSet.KeepAliveIntervalMs = TRUE; return *this; }
    MsH3pSettings(
        const MSH3_SETTINGS* Settings,
        uint32_t SettingsLength
    )
    {
        UNREFERENCED_PARAMETER(SettingsLength); // TODO
        SetSendBufferingEnabled(false);
        SetPeerBidiStreamCount(1000);
        SetPeerUnidiStreamCount(3);
        SetIdleTimeoutMs(30000);
        if (Settings) {
            if (Settings->IsSet.IdleTimeoutMs) {
                SetIdleTimeoutMs(Settings->IdleTimeoutMs);
            }
            if (Settings->IsSet.DisconnectTimeoutMs) {
                SetDisconnectTimeoutMs(Settings->DisconnectTimeoutMs);
            }
            if (Settings->IsSet.KeepAliveIntervalMs) {
                SetKeepAliveIntervalMs(Settings->KeepAliveIntervalMs);
            }
            if (Settings->IsSet.InitialRttMs) {
                SetInitialRttMs(Settings->InitialRttMs);
            }
            if (Settings->IsSet.PeerRequestCount) {
                SetPeerBidiStreamCount(Settings->PeerRequestCount);
            }
            if (Settings->IsSet.DatagramEnabled) {
                SetDatagramReceiveEnabled(Settings->DatagramEnabled);
            }
            if (Settings->IsSet.XdpEnabled) {
                SetXdpEnabled(Settings->XdpEnabled);
            }
        }
    }
};

MsH3pConfiguration::MsH3pConfiguration(
    const MsQuicRegistration& Registration,
    const MSH3_SETTINGS* Settings,
    uint32_t SettingsLength
    ) : MsQuicConfiguration(
            Registration,
            "h3",
            MsH3pSettings(Settings, SettingsLength))
{
    if (Settings) {
        if (Settings->IsSet.DatagramEnabled) {
            DatagramEnabled = Settings->DatagramEnabled;
        }
        if (Settings->IsSet.DynamicQPackEnabled) {
            DynamicQPackEnabled = Settings->DynamicQPackEnabled;
        }
    }
}

MsH3pConfiguration::~MsH3pConfiguration()
{
    if (SelfSign) CxPlatFreeSelfSignedCert(SelfSign);
}

QUIC_CREDENTIAL_FLAGS ToQuicCredFlags(MSH3_CREDENTIAL_FLAGS Flags) {
    QUIC_CREDENTIAL_FLAGS QuicFlags = QUIC_CREDENTIAL_FLAG_NONE;
    if (Flags & MSH3_CREDENTIAL_FLAG_CLIENT) {
        QuicFlags |= QUIC_CREDENTIAL_FLAG_CLIENT;
    }
    if (Flags & MSH3_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION) {
        QuicFlags |= QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
    }
    if (Flags & MSH3_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION) {
        QuicFlags |= QUIC_CREDENTIAL_FLAG_REQUIRE_CLIENT_AUTHENTICATION;
    }
    return QuicFlags;
}

QUIC_CREDENTIAL_CONFIG ToQuicConfig(const MSH3_CREDENTIAL_CONFIG* Config) {
    QUIC_CREDENTIAL_CONFIG QuicConfig {
        .Type = (QUIC_CREDENTIAL_TYPE)Config->Type,
        .Flags = ToQuicCredFlags(Config->Flags),
        .CertificateHash = (QUIC_CERTIFICATE_HASH*)Config->CertificateHash,
        .Principal = nullptr,
        .Reserved = nullptr,
        .AsyncHandler = nullptr,
        .AllowedCipherSuites = QUIC_ALLOWED_CIPHER_SUITE_NONE
    };
    return QuicConfig;
}

MSH3_STATUS
MsH3pConfiguration::LoadH3Credential(
    const MSH3_CREDENTIAL_CONFIG* CredentialConfig
    )
{
    if (CredentialConfig->Type == MSH3_CREDENTIAL_TYPE_SELF_SIGNED_CERTIFICATE) {
        SelfSign = CxPlatGetSelfSignedCert(CXPLAT_SELF_SIGN_CERT_USER, FALSE, NULL);
        if (!SelfSign) return QUIC_STATUS_OUT_OF_MEMORY;
        return LoadCredential(SelfSign);
    }
    auto QuicCredentialConfig = ToQuicConfig(CredentialConfig);
    return LoadCredential(&QuicCredentialConfig);
}

//
// MsH3pConnection
//

MsH3pConnection::MsH3pConnection(
        const MsQuicRegistration& Registration,
        const MSH3_CONNECTION_CALLBACK_HANDLER Handler,
        void* Context
    ) : MsQuicConnection(Registration, CleanUpManual, s_MsQuicCallback, this),
        Callbacks(Handler), Context(Context)
{
    lsqpack_enc_preinit(&Encoder, MSH3_QPACK_LOG_CONTEXT);
    // Initialize with disabled QPACK by default; will be reconfigured in InitializeConfig
    lsqpack_dec_init(&Decoder, MSH3_QPACK_LOG_CONTEXT,
                    0,
                    0,
                    &MsH3pBiDirStream::hset_if,
                    (lsqpack_dec_opts)0);

    if (!IsValid()) return;
    LocalEncoder = new(std::nothrow) MsH3pUniDirStream(*this, H3StreamTypeEncoder);
    if (QUIC_FAILED(InitStatus = LocalEncoder->GetInitStatus())) return;
    LocalDecoder = new(std::nothrow) MsH3pUniDirStream(*this, H3StreamTypeDecoder);
    if (QUIC_FAILED(InitStatus = LocalDecoder->GetInitStatus())) return;
}

MsH3pConnection::MsH3pConnection(
    HQUIC ServerHandle
    ) : MsQuicConnection(ServerHandle, CleanUpManual, s_MsQuicCallback, this)
{
    lsqpack_enc_preinit(&Encoder, MSH3_QPACK_LOG_CONTEXT);
    // Initialize with disabled QPACK by default; will be reconfigured in InitializeConfig
    lsqpack_dec_init(&Decoder, MSH3_QPACK_LOG_CONTEXT,
                    0,
                    0,
                    &MsH3pBiDirStream::hset_if,
                    (lsqpack_dec_opts)0);

    if (!IsValid()) return;
    LocalEncoder = new(std::nothrow) MsH3pUniDirStream(*this, H3StreamTypeEncoder);
    if (QUIC_FAILED(InitStatus = LocalEncoder->GetInitStatus())) return;
    LocalDecoder = new(std::nothrow) MsH3pUniDirStream(*this, H3StreamTypeDecoder);
    if (QUIC_FAILED(InitStatus = LocalDecoder->GetInitStatus())) return;
}

MsH3pConnection::~MsH3pConnection()
{
    lsqpack_enc_cleanup(&Encoder);
    lsqpack_dec_cleanup(&Decoder);
    delete LocalDecoder;
    delete LocalEncoder;
    delete LocalControl;
}

MSH3_STATUS
MsH3pConnection::InitializeConfig(
    const MsH3pConfiguration& Configuration
    )
{
    DynamicQPackEnabled = Configuration.DynamicQPackEnabled;
    LocalControl = new(std::nothrow) MsH3pUniDirStream(*this, Configuration);
    if (QUIC_FAILED(LocalControl->GetInitStatus())) return LocalControl->GetInitStatus();
    return QUIC_STATUS_SUCCESS;
}

MSH3_STATUS
MsH3pConnection::SetConfigurationH3(
    const MsH3pConfiguration& Configuration
    )
{
    QUIC_STATUS Status;
    if (QUIC_FAILED(Status = InitializeConfig(Configuration))) return Status;
    return SetConfiguration(Configuration);
}

MSH3_STATUS
MsH3pConnection::StartH3(
    const MsH3pConfiguration& Configuration,
    const char* ServerName,
    const MSH3_ADDR* ServerAddress
    )
{
    QUIC_STATUS Status;
    if (QUIC_FAILED(Status = InitializeConfig(Configuration))) return Status;
    size_t ServerNameLen = strlen(ServerName);
    if (ServerNameLen >= sizeof(HostName)) {
        return QUIC_STATUS_OUT_OF_MEMORY;
    }
    memcpy(HostName, ServerName, ServerNameLen+1);
    auto QuicAddress = (const QUIC_ADDR*)ServerAddress;
    if (!QuicAddrIsWildCard(QuicAddress) && QUIC_FAILED(Status = SetRemoteAddr(*(QuicAddr*)ServerAddress))) return Status;
    return Start(Configuration, QuicAddrGetFamily(QuicAddress), HostName, QuicAddrGetPort(QuicAddress));
}

QUIC_STATUS
MsH3pConnection::MsQuicCallback(
    _Inout_ QUIC_CONNECTION_EVENT* Event
    )
{
    MSH3_CONNECTION_EVENT h3Event = {};
    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        HandshakeSuccess = true;
        h3Event.Type = MSH3_CONNECTION_EVENT_CONNECTED;
        Callbacks((MSH3_CONNECTION*)this, Context, &h3Event);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        h3Event.Type = MSH3_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT;
        h3Event.SHUTDOWN_INITIATED_BY_TRANSPORT.Status = Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status;
        h3Event.SHUTDOWN_INITIATED_BY_TRANSPORT.ErrorCode = Event->SHUTDOWN_INITIATED_BY_TRANSPORT.ErrorCode;
        Callbacks((MSH3_CONNECTION*)this, Context, &h3Event);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        h3Event.Type = MSH3_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER;
        h3Event.SHUTDOWN_INITIATED_BY_PEER.ErrorCode = Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode;
        Callbacks((MSH3_CONNECTION*)this, Context, &h3Event);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        SetShutdownComplete();
        h3Event.Type = MSH3_CONNECTION_EVENT_SHUTDOWN_COMPLETE;
        Callbacks((MSH3_CONNECTION*)this, Context, &h3Event);
        break;
    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        if (Event->PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL) {
            if (new(std::nothrow) MsH3pUniDirStream(*this, Event->PEER_STREAM_STARTED.Stream) == nullptr) {
                MsQuic->StreamClose(Event->PEER_STREAM_STARTED.Stream);
            }
        } else { // Server scenario
            auto Request = new(std::nothrow) MsH3pBiDirStream(*this, Event->PEER_STREAM_STARTED.Stream);
            if (!Request) return QUIC_STATUS_OUT_OF_MEMORY;
            h3Event.Type = MSH3_CONNECTION_EVENT_NEW_REQUEST;
            h3Event.NEW_REQUEST.Request = (MSH3_REQUEST*)Request;
            Callbacks((MSH3_CONNECTION*)this, Context, &h3Event); // TODO - Check return
        }
        break;
    default: break;
    }
    return QUIC_STATUS_SUCCESS;
}

bool
MsH3pConnection::ReceiveSettingsFrame(
    _In_ uint32_t BufferLength,
    _In_reads_bytes_(BufferLength)
        const uint8_t * const Buffer
    )
{
    uint32_t Offset = 0;

    do {
        QUIC_VAR_INT SettingType, SettingValue;
        if (!MsH3pVarIntDecode(BufferLength, Buffer, &Offset, &SettingType) ||
            !MsH3pVarIntDecode(BufferLength, Buffer, &Offset, &SettingValue)) {
            printf("Not enough settings.\n");
            return false;
        }

        switch (SettingType) {
        case H3SettingQPackMaxTableCapacity:
            PeerMaxTableSize = (uint32_t)SettingValue;
            //printf("[QPACK Debug] Peer QPACK Max Table Size: %u\n", PeerMaxTableSize);
            break;
        case H3SettingMaxFieldSectionSize:
            break; // Ignored for now
        case H3SettingQPackBlockedStreams:
            PeerQPackBlockedStreams = SettingValue;
            //printf("[QPACK Debug] Peer QPACK Blocked Streams: %llu\n", PeerQPackBlockedStreams);
            break;
        case H3SettingEnableConnectProtocol:
            break; // Ignored for now
        case H3SettingDatagrams:
            if (SettingValue) {
                // TODO
            }
            break;
        default:
            //printf("Unknown/unsupported setting type: 0x%llx\n", (unsigned long long)SettingType);
            break;
        }

    } while (Offset < BufferLength);

    tsu_buf_sz = sizeof(tsu_buf);

    uint32_t dynamicTableSize = min(PeerMaxTableSize, GetQPackMaxTableCapacity(DynamicQPackEnabled));
    uint64_t blockedStreams = min(PeerQPackBlockedStreams, GetQPackBlockedStreams(DynamicQPackEnabled));

    //printf("[QPACK Debug] Initializing encoder/decoder with dynamicTableSize=%u, blockedStreams=%llu (DynamicQPackEnabled=%s, PeerMaxTableSize=%u, PeerQPackBlockedStreams=%llu)\n",
    //       dynamicTableSize, blockedStreams, DynamicQPackEnabled ? "true" : "false", PeerMaxTableSize, PeerQPackBlockedStreams);

    // Initialize the encoder
    if (lsqpack_enc_init(&Encoder, MSH3_QPACK_LOG_CONTEXT, dynamicTableSize, dynamicTableSize, (unsigned)blockedStreams, LSQPACK_ENC_OPT_STAGE_2, tsu_buf, &tsu_buf_sz) != 0) {
        printf("lsqpack_enc_init failed\n");
        return false;
    }

    // Re-initialize the decoder to match the encoder settings
    // This ensures encoder/decoder compatibility regardless of peer capabilities
    lsqpack_dec_cleanup(&Decoder);
    lsqpack_dec_init(&Decoder, MSH3_QPACK_LOG_CONTEXT, dynamicTableSize, (unsigned)blockedStreams, &MsH3pBiDirStream::hset_if, (lsqpack_dec_opts)0);

    return true;
}

//
// MsH3pUniDirStream
//

MsH3pUniDirStream::MsH3pUniDirStream(MsH3pConnection& Connection, H3StreamType Type)
    : MsQuicStream(Connection, QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL | QUIC_STREAM_OPEN_FLAG_0_RTT, CleanUpManual, s_MsQuicCallback, this), H3(Connection), Type(Type)
{
    if (!IsValid()) return;
    Buffer.Buffer[0] = (uint8_t)Type;
    Buffer.Length = 1;
    DebugIoBuffer(&Buffer, "send", Type);
    InitStatus = Send(&Buffer, 1, QUIC_SEND_FLAG_ALLOW_0_RTT | QUIC_SEND_FLAG_START);
}

MsH3pUniDirStream::MsH3pUniDirStream(MsH3pConnection& Connection, const MsH3pConfiguration& Configuration)
    : MsQuicStream(Connection, QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL | QUIC_STREAM_OPEN_FLAG_0_RTT, CleanUpManual, s_MsQuicCallback, this), H3(Connection), Type(H3StreamTypeControl)
{
    if (!IsValid()) return;
    Buffer.Buffer[0] = (uint8_t)Type;
    Buffer.Length = 1;

    H3Settings Settings[3];
    uint32_t SettingsLength = 0;
    Settings[SettingsLength++] = { H3SettingQPackMaxTableCapacity, GetQPackMaxTableCapacity(Configuration.DynamicQPackEnabled) };
    Settings[SettingsLength++] = { H3SettingQPackBlockedStreams, GetQPackBlockedStreams(Configuration.DynamicQPackEnabled) };
    if (Configuration.DatagramEnabled) {
        Settings[SettingsLength++] = { H3SettingDatagrams, 1 };
    }

    if (!H3WriteSettingsFrame(Settings, SettingsLength, &Buffer.Length, sizeof(RawBuffer), RawBuffer)) {
        InitStatus = QUIC_STATUS_OUT_OF_MEMORY;
        return;
    }
    DebugIoBuffer(&Buffer, "send", Type);
    InitStatus = Send(&Buffer, 1, QUIC_SEND_FLAG_ALLOW_0_RTT | QUIC_SEND_FLAG_START);
}

MsH3pUniDirStream::MsH3pUniDirStream(MsH3pConnection& Connection, const HQUIC StreamHandle)
    : MsQuicStream(StreamHandle, CleanUpAutoDelete, s_MsQuicCallback, this), H3(Connection), Type(H3StreamTypeUnknown)
{ }

QUIC_STATUS
MsH3pUniDirStream::ControlStreamCallback(
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
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        break;
    default: break;
    }
    return QUIC_STATUS_SUCCESS;
}

void
MsH3pUniDirStream::ControlReceive(
    _In_ const QUIC_BUFFER* RecvBuffer
    )
{
    uint32_t Offset = 0;

    DebugIoBuffer(RecvBuffer, "recv", Type);

    do {
        QUIC_VAR_INT FrameType, FrameLength;
        if (!MsH3pVarIntDecode(RecvBuffer->Length, RecvBuffer->Buffer, &Offset, &FrameType) ||
            !MsH3pVarIntDecode(RecvBuffer->Length, RecvBuffer->Buffer, &Offset, &FrameLength)) {
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
MsH3pUniDirStream::EncodeHeaders(
    _In_ MsH3pBiDirStream* Request,
    _In_reads_(HeadersCount)
        const MSH3_HEADER* Headers,
    _In_ size_t HeadersCount
    )
{
    auto StreamId = Request->ID();
    if (StreamId > QUIC_UINT62_MAX) {
        return false; // Stream failed to start
    }

    if (lsqpack_enc_start_header(&H3.Encoder, StreamId, 0) != 0) {
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

        // Use dynamic table if possible - don't set LQEF_NO_INDEX or LQEF_NEVER_INDEX flags
        // This allows the encoder to decide whether to index the header
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

    // If there's encoder output from dynamic indexing, send it on the encoder stream
    if (Buffer.Length != 0) {
        DebugIoBuffer(&Buffer, "send", Type);
        if (QUIC_FAILED(Send(&Buffer, 1, QUIC_SEND_FLAG_ALLOW_0_RTT))) {
            printf("Encoder send failed\n");
        }
    }

    return true;
}

void
MsH3pUniDirStream::SendQPackAcknowledgment(
    _In_ uint64_t StreamId
    )
{
    // TODO: This function reuses the same Buffer and RawBuffer as other functions,
    // as well as duplicate calls of this function. It's possible they are still
    // being used by MsQuic. We need to dynamically allocate (from a pool?) these
    // to avoid collisions.

    // QPACK Section Acknowledgment instruction format:
    // 1xxxxxxx where xxxxxxx is the Stream ID encoded as a 7-bit prefix integer
    Buffer.Length = 0;

    if (StreamId < 127) {
        RawBuffer[0] = 0x80 | (unsigned char)StreamId;
        Buffer.Length = 1;
    } else {
        RawBuffer[0] = 0x80 | 0x7F;
        Buffer.Length = 1;
        uint64_t remaining = StreamId - 127;
        while (remaining >= 128) {
            RawBuffer[Buffer.Length++] = 0x80 | (remaining & 0x7F);
            remaining >>= 7;
        }
        RawBuffer[Buffer.Length++] = remaining & 0x7F;
    }

    DebugIoBuffer(&Buffer, "send", H3StreamTypeEncoder);
    auto Status = Send(&Buffer, 1, QUIC_SEND_FLAG_NONE);
    if (QUIC_FAILED(Status)) {
        printf("[QPACK] Failed to send Section Acknowledgment for stream %llu: 0x%x\n", 
            (long long unsigned)StreamId, Status);
    }
}

void
MsH3pUniDirStream::SendQPackStreamInstructions()
{
    // Send any pending Insert Count Increment instructions
    if (lsqpack_dec_ici_pending(&H3.Decoder)) {
        Buffer.Length = (uint32_t)lsqpack_dec_write_ici(&H3.Decoder, RawBuffer, sizeof(RawBuffer));
        if (Buffer.Length > 0) {
            auto Status = Send(&Buffer, 1, QUIC_SEND_FLAG_NONE);
            if (QUIC_FAILED(Status)) {
                printf("[QPACK] Failed to send ICI instruction: 0x%x\n", Status);
            }
        }
    }
}

void
MsH3pUniDirStream::SendStreamCancellation(
    _In_ uint64_t StreamId
    )
{
    Buffer.Length = (uint32_t)lsqpack_dec_cancel_stream_id(&H3.Decoder, StreamId, RawBuffer, sizeof(RawBuffer));
    if (Buffer.Length > 0) {
        auto Status = Send(&Buffer, 1, QUIC_SEND_FLAG_NONE);
        if (QUIC_FAILED(Status)) {
            printf("[QPACK] Failed to send Stream Cancellation for stream %llu: 0x%x\n", 
                (long long unsigned)StreamId, Status);
        }
    }
}

QUIC_STATUS
MsH3pUniDirStream::EncoderStreamCallback(
    _Inout_ QUIC_STREAM_EVENT* Event
    )
{
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_RECEIVE:
        // Process encoder stream data for dynamic table updates
        for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; ++i) {
            const QUIC_BUFFER* Buffer = Event->RECEIVE.Buffers + i;

            DebugIoBuffer(Buffer, "recv", Type);

            if (Buffer->Length > 0) {
                // Feed encoder instructions to the QPACK decoder
                int ret = lsqpack_dec_enc_in(&H3.Decoder,
                                           Buffer->Buffer,
                                           Buffer->Length);

                if (ret != 0) {
                    printf("[QPACK] lsqpack_dec_enc_in failed: %d\n", ret);
                }
            }
        }
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        break;
    default: break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS
MsH3pUniDirStream::DecoderStreamCallback(
    _Inout_ QUIC_STREAM_EVENT* Event
    )
{
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_RECEIVE:
        for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; ++i) {
            const QUIC_BUFFER* Buffer = Event->RECEIVE.Buffers + i;

            DebugIoBuffer(Buffer, "recv", Type);

            if (Buffer->Length > 0) {
                // Process decoder instructions from peer
                int ret = lsqpack_enc_decoder_in(&H3.Encoder,
                                                Buffer->Buffer,
                                                Buffer->Length);

                if (ret != 0) {
                    printf("[QPACK] lsqpack_enc_decoder_in failed: %d\n", ret);
                }
            }
        }
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        break;
    default: break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS
MsH3pUniDirStream::UnknownStreamCallback(
    _Inout_ QUIC_STREAM_EVENT* Event
    )
{
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_RECEIVE:
        if (Event->RECEIVE.TotalBufferLength > 0) {
            auto FirstBuffer = (QUIC_BUFFER*)Event->RECEIVE.Buffers;
            auto NewType = FirstBuffer->Buffer[0];
            FirstBuffer->Buffer++; FirstBuffer->Length--;
            switch (NewType) {
            case H3StreamTypeControl:
                Type = H3StreamTypeControl;
                H3.PeerControl = this;
                ControlStreamCallback(Event);
                break;
            case H3StreamTypeEncoder:
                Type = H3StreamTypeEncoder;
                H3.PeerEncoder = this;
                EncoderStreamCallback(Event);
                break;
            case H3StreamTypeDecoder:
                Type = H3StreamTypeDecoder;
                H3.PeerDecoder = this;
                DecoderStreamCallback(Event);
                break;
            default:
                break;
            }
        }
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        break;
    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        break;
    default: break;
    }
    return QUIC_STATUS_SUCCESS;
}

//
// MsH3pBiDirStream
//

struct lsqpack_dec_hset_if
MsH3pBiDirStream::hset_if = {
    .dhi_unblocked      = s_DecodeUnblocked,
    .dhi_prepare_decode = s_DecodePrepare,
    .dhi_process_header = s_DecodeProcess,
};

bool
MsH3pBiDirStream::Send(
    _In_ MSH3_REQUEST_SEND_FLAGS Flags,
    _In_reads_(HeadersCount)
        const MSH3_HEADER* Headers,
    _In_ size_t HeadersCount,
    _In_reads_bytes_(DataLength) const void* Data,
    _In_ uint32_t DataLength,
    _In_opt_ void* AppContext
    )
{
    if (Headers && HeadersCount != 0) { // TODO - Make sure headers weren't already sent
        if (!H3.LocalEncoder->EncodeHeaders(this, Headers, HeadersCount)) return false;
        auto HeadersLength = Buffers[1].Length + Buffers[2].Length;
        auto HeaderFlags = Flags;
        if (Data && DataLength != 0) {
            HeaderFlags &= ~MSH3_REQUEST_SEND_FLAG_FIN;
            HeaderFlags |= MSH3_REQUEST_SEND_FLAG_DELAY_SEND;
        }
        if (!H3WriteFrameHeader(H3FrameHeaders, HeadersLength, &Buffers[0].Length, sizeof(FrameHeaderBuffer), FrameHeaderBuffer) ||
            QUIC_FAILED(MsQuicStream::Send(Buffers, 3, ToQuicSendFlags(HeaderFlags)))) {
            return false;
        }
    }
    if (Data && DataLength != 0) {
        auto AppSend = new(std::nothrow) MsH3pAppSend(AppContext); // TODO - Pool alloc
        if (!AppSend || !AppSend->SetData(Data, DataLength) ||
            QUIC_FAILED(MsQuicStream::Send(AppSend->Buffers, 2, ToQuicSendFlags(Flags), AppSend))) {
            delete AppSend;
            return false;
        }
    }
    return true;
}

QUIC_STATUS
MsH3pBiDirStream::MsQuicCallback(
    _Inout_ QUIC_STREAM_EVENT* Event
    )
{
    MSH3_REQUEST_EVENT h3Event = {};
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_START_COMPLETE:
        if (QUIC_FAILED(Event->START_COMPLETE.Status)) {
            if (!Complete) {
                h3Event.Type = MSH3_REQUEST_EVENT_SEND_SHUTDOWN_COMPLETE;
                h3Event.SEND_SHUTDOWN_COMPLETE.Graceful = false;
                Callbacks((MSH3_REQUEST*)this, Context, &h3Event);
            }
            Complete = true;
            ShutdownComplete = true;
            h3Event.Type = MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE;
            h3Event.SHUTDOWN_COMPLETE.ConnectionShutdown = false;
            h3Event.SHUTDOWN_COMPLETE.AppCloseInProgress = false;
            h3Event.SHUTDOWN_COMPLETE.ConnectionShutdownByApp = false;
            h3Event.SHUTDOWN_COMPLETE.ConnectionClosedRemotely = false;
            h3Event.SHUTDOWN_COMPLETE.RESERVED = false;
            h3Event.SHUTDOWN_COMPLETE.ConnectionErrorCode = 0;
            h3Event.SHUTDOWN_COMPLETE.ConnectionCloseStatus = Event->START_COMPLETE.Status;
            Callbacks((MSH3_REQUEST*)this, Context, &h3Event);
        }
        break;
    case QUIC_STREAM_EVENT_RECEIVE:
        return Receive(Event);
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        if (Event->SEND_COMPLETE.ClientContext) {
            auto AppSend = (MsH3pAppSend*)Event->SEND_COMPLETE.ClientContext;
            h3Event.Type = MSH3_REQUEST_EVENT_SEND_COMPLETE;
            h3Event.SEND_COMPLETE.Canceled = FALSE;
            h3Event.SEND_COMPLETE.ClientContext = AppSend->AppContext;
            Callbacks((MSH3_REQUEST*)this, Context, &h3Event);
            delete AppSend;
        }
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        Complete = true;
        h3Event.Type = MSH3_REQUEST_EVENT_PEER_SEND_SHUTDOWN;
        Callbacks((MSH3_REQUEST*)this, Context, &h3Event);
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        Complete = true;
        h3Event.Type = MSH3_REQUEST_EVENT_PEER_SEND_ABORTED;
        h3Event.PEER_SEND_ABORTED.ErrorCode = Event->PEER_SEND_ABORTED.ErrorCode;
        Callbacks((MSH3_REQUEST*)this, Context, &h3Event);
        break;
    case QUIC_STREAM_EVENT_SEND_SHUTDOWN_COMPLETE:
        h3Event.Type = MSH3_REQUEST_EVENT_SEND_SHUTDOWN_COMPLETE;
        h3Event.SEND_SHUTDOWN_COMPLETE.Graceful = Event->SEND_SHUTDOWN_COMPLETE.Graceful;
        Callbacks((MSH3_REQUEST*)this, Context, &h3Event);
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        if (!ShutdownComplete) { // TODO - Need better logic here?
            h3Event.Type = MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE;
            h3Event.SHUTDOWN_COMPLETE.ConnectionShutdown = Event->SHUTDOWN_COMPLETE.ConnectionShutdown;
            h3Event.SHUTDOWN_COMPLETE.AppCloseInProgress = Event->SHUTDOWN_COMPLETE.AppCloseInProgress;
            h3Event.SHUTDOWN_COMPLETE.ConnectionShutdownByApp = Event->SHUTDOWN_COMPLETE.ConnectionShutdownByApp;
            h3Event.SHUTDOWN_COMPLETE.ConnectionClosedRemotely = Event->SHUTDOWN_COMPLETE.ConnectionClosedRemotely;
            h3Event.SHUTDOWN_COMPLETE.RESERVED = Event->SHUTDOWN_COMPLETE.RESERVED;
            h3Event.SHUTDOWN_COMPLETE.ConnectionErrorCode = Event->SHUTDOWN_COMPLETE.ConnectionErrorCode;
            h3Event.SHUTDOWN_COMPLETE.ConnectionCloseStatus = Event->SHUTDOWN_COMPLETE.ConnectionCloseStatus;
            Callbacks((MSH3_REQUEST*)this, Context, &h3Event);
        }
        break;
    case QUIC_STREAM_EVENT_IDEAL_SEND_BUFFER_SIZE:
        h3Event.Type = MSH3_REQUEST_EVENT_IDEAL_SEND_SIZE;
        h3Event.IDEAL_SEND_SIZE.ByteCount = Event->IDEAL_SEND_BUFFER_SIZE.ByteCount;
        Callbacks((MSH3_REQUEST*)this, Context, &h3Event);
        break;
    //case QUIC_STREAM_EVENT_PEER_ACCEPTED: break; // TODO - Indicate up?
    default: break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS
MsH3pBiDirStream::Receive(
    _Inout_ QUIC_STREAM_EVENT* Event
    )
{
    for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; ++i) {
        const QUIC_BUFFER* Buffer = Event->RECEIVE.Buffers + i;
        do {
            if (CurFrameLengthLeft == 0) { // Not in the middle of reading frame payload
                if (BufferedHeadersLength == 0) { // No partial frame header bufferred
                    if (!MsH3pVarIntDecode(Buffer->Length, Buffer->Buffer, &CurRecvOffset, &CurFrameType) ||
                        !MsH3pVarIntDecode(Buffer->Length, Buffer->Buffer, &CurRecvOffset, &CurFrameLength)) {
                        BufferedHeadersLength = Buffer->Length - CurRecvOffset;
                        memcpy(BufferedHeaders, Buffer->Buffer + CurRecvOffset, BufferedHeadersLength);
                        break;
                    }
                } else { // Partial frame header bufferred already
                    uint32_t ToCopy = sizeof(BufferedHeaders) - BufferedHeadersLength;
                    if (ToCopy > Buffer->Length) ToCopy = Buffer->Length;
                    memcpy(BufferedHeaders + BufferedHeadersLength, Buffer->Buffer, ToCopy);
                    if (!MsH3pVarIntDecode(BufferedHeadersLength+ToCopy, BufferedHeaders, &CurRecvOffset, &CurFrameType) ||
                        !MsH3pVarIntDecode(BufferedHeadersLength+ToCopy, BufferedHeaders, &CurRecvOffset, &CurFrameLength)) {
                        BufferedHeadersLength += ToCopy;
                        break;
                    }
                    CurRecvOffset -= BufferedHeadersLength;
                    BufferedHeadersLength = 0;
                }
                CurFrameLengthLeft = CurFrameLength;
            }

            uint32_t AvailFrameLength;
            if (CurRecvOffset + CurFrameLengthLeft > (uint64_t)Buffer->Length) {
                AvailFrameLength = Buffer->Length - CurRecvOffset; // Rest of the buffer
            } else {
                AvailFrameLength = (uint32_t)CurFrameLengthLeft;
            }

            if (CurFrameType == H3FrameData) {
                ReceivePending = true;
                MSH3_REQUEST_EVENT h3Event = {};
                h3Event.Type = MSH3_REQUEST_EVENT_DATA_RECEIVED;
                h3Event.DATA_RECEIVED.Data = Buffer->Buffer + CurRecvOffset;
                h3Event.DATA_RECEIVED.Length = AvailFrameLength;
                MSH3_STATUS Status = Callbacks((MSH3_REQUEST*)this, Context, &h3Event);
                if (Status == MSH3_STATUS_SUCCESS) {
                    ReceivePending = false; // Not pending receive
                    if (h3Event.DATA_RECEIVED.Length < AvailFrameLength) { // Partial receive case
                        CurFrameLengthLeft -= h3Event.DATA_RECEIVED.Length;
                        Event->RECEIVE.TotalBufferLength =
                            CurRecvCompleteLength + CurRecvOffset + h3Event.DATA_RECEIVED.Length;
                        CurRecvCompleteLength = 0;
                        CurRecvOffset = 0;
                        return QUIC_STATUS_SUCCESS;
                    }
                } else if (Status == MSH3_STATUS_PENDING) { // Receive pending (but may have been completed via API call already)
                    if (!ReceivePending) {
                        // TODO - Support continuing this receive since it was completed via the API call
                    }
                    return QUIC_STATUS_PENDING;
                } else {
                    // TODO - Assert
                }
            } else if (CurFrameType == H3FrameHeaders) {
                const uint8_t* Frame = Buffer->Buffer + CurRecvOffset;
                if (CurFrameLengthLeft == CurFrameLength) {
                    auto StreamId = ID();
                    auto rhs =
                        lsqpack_dec_header_in(
                            &H3.Decoder, this, StreamId, (size_t)CurFrameLength, &Frame,
                            AvailFrameLength, nullptr, nullptr);
                    // LQRHS_NEED is expected - it means we need more header block data
                    // LQRHS_BLOCKED is also expected - it means we need more encoder stream data
                    if (rhs == LQRHS_ERROR) {
                        printf("lsqpack_dec_header_in error\n");
                    } else if (rhs == LQRHS_BLOCKED) {
                        printf("[QPACK Debug] Header block blocked, waiting for encoder stream data\n");
                    } else if (rhs == LQRHS_NEED) {
                        //printf("[QPACK Debug] Header block needs more data\n");
                    } else if (H3.DynamicQPackEnabled) { // LQRHS_DONE
                        H3.LocalDecoder->SendQPackAcknowledgment(StreamId);
                    }
                } else { // Continued from a previous partial read
                    auto rhs =
                        lsqpack_dec_header_read(
                            &H3.Decoder, this, &Frame, AvailFrameLength, nullptr,
                            nullptr);
                    // LQRHS_NEED is expected - it means we need more header block data
                    // LQRHS_BLOCKED is also expected - it means we need more encoder stream data
                    if (rhs == LQRHS_ERROR) {
                        printf("lsqpack_dec_header_read error\n");
                    } else if (rhs == LQRHS_BLOCKED) {
                        printf("[QPACK Debug] Header read blocked, waiting for encoder stream data\n");
                    } else if (rhs == LQRHS_NEED) {
                        //printf("[QPACK Debug] Header read needs more data\n");
                    } else if (H3.DynamicQPackEnabled) { // LQRHS_DONE
                        H3.LocalDecoder->SendQPackAcknowledgment(ID());
                    }
                }
            }

            CurFrameLengthLeft -= AvailFrameLength;
            CurRecvOffset += AvailFrameLength;

        } while (CurRecvOffset < Buffer->Length);

        CurRecvCompleteLength += Buffer->Length;
        CurRecvOffset = 0;
    }

    CurRecvCompleteLength = 0;

    return QUIC_STATUS_SUCCESS;
}

void
MsH3pBiDirStream::CompleteReceive(
    _In_ uint32_t Length
    )
{
    if (ReceivePending) {
        ReceivePending = false;
        CurFrameLengthLeft -= Length;
        auto CompleteLength = CurRecvCompleteLength + CurRecvOffset + Length;
        CurRecvCompleteLength = 0;
        CurRecvOffset = 0;
        (void)ReceiveComplete(CompleteLength);
    }
}

struct lsxpack_header*
MsH3pBiDirStream::DecodePrepare(
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
MsH3pBiDirStream::DecodeProcess(
    struct lsxpack_header* Header
    )
{
    const MSH3_HEADER h {
        .Name = Header->buf + Header->name_offset,
        .NameLength = Header->name_len,
        .Value = Header->buf + Header->val_offset,
        .ValueLength = Header->val_len };
    MSH3_REQUEST_EVENT h3Event = {};
    h3Event.Type = MSH3_REQUEST_EVENT_HEADER_RECEIVED;
    h3Event.HEADER_RECEIVED.Header = &h;
    Callbacks((MSH3_REQUEST*)this, Context, &h3Event);
}

//
// MsH3pListener
//

MsH3pListener::MsH3pListener(
    const MsQuicRegistration& Registration,
    const MSH3_ADDR* Address,
    const MSH3_LISTENER_CALLBACK_HANDLER Handler,
    void* Context
    ) : MsQuicListener(Registration, CleanUpManual, s_MsQuicCallback, this),
        Callbacks(Handler), Context(Context)
{
    if (QUIC_SUCCEEDED(InitStatus)) {
        InitStatus = Start("h3", (QUIC_ADDR*)Address);
    }
}

QUIC_STATUS
MsH3pListener::MsQuicCallback(
    _Inout_ QUIC_LISTENER_EVENT* Event
    )
{
    if (Event->Type == QUIC_LISTENER_EVENT_NEW_CONNECTION) {
        auto Connection = new(std::nothrow) MsH3pConnection(Event->NEW_CONNECTION.Connection);
        if (!Connection) return QUIC_STATUS_OUT_OF_MEMORY;
        MSH3_LISTENER_EVENT h3Event = {};
        h3Event.Type = MSH3_LISTENER_EVENT_NEW_CONNECTION;
        h3Event.NEW_CONNECTION.Connection = (MSH3_CONNECTION*)Connection;
        h3Event.NEW_CONNECTION.ServerName = Event->NEW_CONNECTION.Info->ServerName;
        h3Event.NEW_CONNECTION.ServerNameLength = Event->NEW_CONNECTION.Info->ServerNameLength;
        Callbacks((MSH3_LISTENER*)this, Context, &h3Event);
    }
    return QUIC_STATUS_SUCCESS;
}
