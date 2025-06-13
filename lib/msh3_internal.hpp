/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#pragma once

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable:4244) // LSQpack int conversion
#pragma warning(disable:4267) // LSQpack int conversion
#endif

#define MSH3_TEST_MODE 1 // Always built in if server is supported
#define QUIC_TEST_APIS 1
#include <quic_platform.h>

#include <msquic.hpp>
#include <lsqpack.h>
#include <lsxpack_header.h>
#include <stdio.h>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#ifdef _WIN32
#pragma warning(pop)
#endif

#include "msh3.h"
#define MSH3_VERSION_ONLY 1
#include "msh3.ver"

#define MSH3_DEBUG_IO 0                 // Print out the contents of the uni-directional streams
#define MSH3_QPACK_LOG_CONTEXT nullptr  // QPACK logging context, set to stdout for debugging

#ifdef _WIN32
#define CxPlatByteSwapUint16 _byteswap_ushort
#define CxPlatByteSwapUint32 _byteswap_ulong
#define CxPlatByteSwapUint64 _byteswap_uint64
#else
#define CxPlatByteSwapUint16(value) __builtin_bswap16((unsigned short)(value))
#define CxPlatByteSwapUint32(value) __builtin_bswap32((value))
#define CxPlatByteSwapUint64(value) __builtin_bswap64((value))
#endif

#ifndef ARRAYSIZE
#define ARRAYSIZE(A) (sizeof(A)/sizeof((A)[0]))
#endif

#ifndef CXPLAT_ANALYSIS_ASSERT
#define CXPLAT_ANALYSIS_ASSERT(X)
#endif

#ifndef min
#define min(a,b) ((a) > (b) ? (b) : (a))
#endif

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) (void)(P)
#endif

#include <quic_var_int.h>

enum H3SettingsType {
    // https://datatracker.ietf.org/doc/html/rfc9204#section-8.1 (QPACK)
    H3SettingQPackMaxTableCapacity      = 1,
    // https://datatracker.ietf.org/doc/html/rfc9113#section-6.5.2 (HTTP/2, unused?)
    H3SettingEnablePush                 = 2,
    H3SettingMaxConcurrentStreams       = 3,
    H3SettingInitialWindowSize          = 4,
    H3SettingMaxFrameSize               = 5,
    // https://datatracker.ietf.org/doc/html/rfc9114#section-7.2.4.1 (HTTP/3)
    H3SettingMaxFieldSectionSize        = 6,
    // https://datatracker.ietf.org/doc/html/rfc9204#section-8.1 (QPACK)
    H3SettingQPackBlockedStreams        = 7,
    // https://datatracker.ietf.org/doc/html/rfc9220#section-3 (WebSockets over HTTP/3)
    H3SettingEnableConnectProtocol      = 8,
    // https://datatracker.ietf.org/doc/html/rfc9297#section-2.1.1
    H3SettingDatagrams                  = 0x33,
};

// Contiguous buffer for (non-null-terminated) header name and value strings.
struct H3HeadingPair : public lsxpack_header_t {
    char Buffer[512] = {0};
    H3HeadingPair() { memset(this, 0, sizeof(lsxpack_header_t)); }
    bool Set(const MSH3_HEADER* Header) {
        if (Header->NameLength + Header->ValueLength > sizeof(Buffer)) return false;
        buf = Buffer;
        name_offset = 0;
        name_len = (lsxpack_strlen_t)Header->NameLength;
        val_offset = name_len;
        val_len = (lsxpack_strlen_t)Header->ValueLength;
        memcpy(Buffer, Header->Name, name_len);
        memcpy(Buffer+name_len, Header->Value, val_len);
        return true;
    }
};

struct H3Headers {
    H3HeadingPair* Pairs;
    uint32_t PairCount;
};

struct H3Settings {
    H3SettingsType Type;
    uint64_t Integer;
};

enum H3StreamType {
    H3StreamTypeUnknown = 0xFF,
    H3StreamTypeControl = 0,
    H3StreamTypePush    = 1,
    H3StreamTypeEncoder = 2,
    H3StreamTypeDecoder = 3,
};

enum H3FrameType {
    H3FrameData         = 0,
    H3FrameHeaders      = 1,
    H3FramePriority     = 2,
    H3FrameCancelPush   = 3,
    H3FrameSettings     = 4,
    H3FramePushPromise  = 5,
    H3FrameGoaway       = 7,
    H3FrameUnknown      = 0xFF
};

#define H3_RFC_DEFAULT_HEADER_TABLE_SIZE    0
#define H3_RFC_DEFAULT_QPACK_BLOCKED_STREAM 0

// Helper functions to get QPACK settings based on configuration
inline uint32_t GetQPackMaxTableCapacity(bool DynamicQPackEnabled) {
    return DynamicQPackEnabled ? 4096 : 0;  // Enable dynamic table with a default size of 4096 bytes
}

inline uint32_t GetQPackBlockedStreams(bool DynamicQPackEnabled) {
    return DynamicQPackEnabled ? 100 : 0;   // Allow up to 100 blocked streams
}

// Copied from QuicVanIntDecode and changed to uint32_t offset/length
inline
_Success_(return != FALSE)
BOOLEAN
MsH3pVarIntDecode(
    _In_ uint32_t BufferLength,
    _In_reads_bytes_(BufferLength)
        const uint8_t * const Buffer,
    _Inout_
    _Deref_in_range_(0, BufferLength)
    _Deref_out_range_(0, BufferLength)
        uint32_t* Offset,
    _Out_ QUIC_VAR_INT* Value
    )
{
    if (BufferLength < sizeof(uint8_t) + *Offset) {
        return FALSE;
    }
    if (Buffer[*Offset] < 0x40) {
        *Value = Buffer[*Offset];
        CXPLAT_ANALYSIS_ASSERT(*Value < 0x100ULL);
        *Offset += sizeof(uint8_t);
    } else if (Buffer[*Offset] < 0x80) {
        if (BufferLength < sizeof(uint16_t) + *Offset) {
            return FALSE;
        }
        *Value = ((uint64_t)(Buffer[*Offset] & 0x3fUL)) << 8;
        *Value |= Buffer[*Offset + 1];
        CXPLAT_ANALYSIS_ASSERT(*Value < 0x10000ULL);
        *Offset += sizeof(uint16_t);
    } else if (Buffer[*Offset] < 0xc0) {
        if (BufferLength < sizeof(uint32_t) + *Offset) {
            return FALSE;
        }
        uint32_t v;
        memcpy(&v, Buffer + *Offset, sizeof(uint32_t));
        *Value = CxPlatByteSwapUint32(v) & 0x3fffffffUL;
        CXPLAT_ANALYSIS_ASSERT(*Value < 0x100000000ULL);
        *Offset += sizeof(uint32_t);
    } else {
        if (BufferLength < sizeof(uint64_t) + *Offset) {
            return FALSE;
        }
        uint64_t v;
        memcpy(&v, Buffer + *Offset, sizeof(uint64_t));
        *Value = CxPlatByteSwapUint64(v) & 0x3fffffffffffffffULL;
        *Offset += sizeof(uint64_t);
    }
    return TRUE;
}

inline bool
H3WriteFrameHeader(
    _In_ uint8_t Type,
    _In_ uint32_t Length,
    _Inout_ uint32_t* Offset,
    _In_ uint32_t BufferLength,
    _Out_writes_to_(BufferLength, *Offset)
        uint8_t* Buffer
    )
{
    const uint32_t RequiredLength =
        QuicVarIntSize(Type) +
        QuicVarIntSize(Length);
    if (BufferLength < *Offset + RequiredLength) {
        return false;
    }
    Buffer = Buffer + *Offset;
    Buffer = QuicVarIntEncode(Type, Buffer);
    Buffer = QuicVarIntEncode(Length, Buffer);
    *Offset += RequiredLength;
    return true;
}

inline bool
H3WriteSettingsFrame(
    _In_reads_(SettingsCount)
        const H3Settings* Settings,
    _In_ uint32_t SettingsCount,
    _Inout_ uint32_t* Offset,
    _In_ uint32_t BufferLength,
    _Out_writes_to_(BufferLength, *Offset)
        uint8_t* Buffer
    )
{
    uint32_t PayloadSize = 0;
    for (uint32_t i = 0; i < SettingsCount; i++) {
        PayloadSize += QuicVarIntSize(Settings[i].Type);
        PayloadSize += QuicVarIntSize(Settings[i].Integer);
    }
    if (!H3WriteFrameHeader(
            H3FrameSettings,
            PayloadSize,
            Offset,
            BufferLength,
            Buffer)) {
        return false;
    }
    if (BufferLength < *Offset + PayloadSize) {
        return false;
    }
    Buffer = Buffer + *Offset;
    for (uint32_t i = 0; i < SettingsCount; i++) {
        Buffer = QuicVarIntEncode(Settings[i].Type, Buffer);
        Buffer = QuicVarIntEncode(Settings[i].Integer, Buffer);
    }
    *Offset += PayloadSize;
    return true;
}

inline QUIC_STREAM_OPEN_FLAGS ToQuicOpenFlags(MSH3_REQUEST_FLAGS Flags) {
    return Flags & MSH3_REQUEST_FLAG_ALLOW_0_RTT ? QUIC_STREAM_OPEN_FLAG_0_RTT : QUIC_STREAM_OPEN_FLAG_NONE;
}

inline QUIC_SEND_FLAGS ToQuicSendFlags(MSH3_REQUEST_SEND_FLAGS Flags) {
    QUIC_SEND_FLAGS QuicFlags = QUIC_SEND_FLAG_NONE;
    if (Flags & MSH3_REQUEST_SEND_FLAG_ALLOW_0_RTT) {
        QuicFlags |= QUIC_SEND_FLAG_ALLOW_0_RTT;
    }
    if (Flags & MSH3_REQUEST_SEND_FLAG_FIN) {
        QuicFlags |= QUIC_SEND_FLAG_START | QUIC_SEND_FLAG_FIN;
    } else if (Flags & MSH3_REQUEST_SEND_FLAG_DELAY_SEND) {
        QuicFlags |= QUIC_SEND_FLAG_DELAY_SEND; // TODO - Add support for a _START_DELAYED flag in MsQuic?
    } else {
        QuicFlags |= QUIC_SEND_FLAG_START;
    }
    return QuicFlags;
}

inline QUIC_STREAM_SHUTDOWN_FLAGS ToQuicShutdownFlags(MSH3_REQUEST_SHUTDOWN_FLAGS Flags) {
    QUIC_STREAM_SHUTDOWN_FLAGS QuicFlags = QUIC_STREAM_SHUTDOWN_FLAG_NONE;
    if (Flags & MSH3_REQUEST_SHUTDOWN_FLAG_GRACEFUL) {
        QuicFlags |= QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL;
    } else {
        if (Flags & MSH3_REQUEST_SHUTDOWN_FLAG_ABORT_SEND) {
            QuicFlags |= QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND;
        }
        if (Flags & MSH3_REQUEST_SHUTDOWN_FLAG_ABORT_RECEIVE) {
            QuicFlags |= QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE;
        }
    }
    return QuicFlags;
}

struct MsH3pConfiguration : public MsQuicConfiguration {
    bool DatagramEnabled {false};
    bool DynamicQPackEnabled {false};
    QUIC_CREDENTIAL_CONFIG* SelfSign {nullptr};
    MsH3pConfiguration(
        const MsQuicRegistration& Registration,
        const MSH3_SETTINGS* Settings,
        uint32_t SettingsLength
        );
    ~MsH3pConfiguration();
    MSH3_STATUS
    LoadH3Credential(
        const MSH3_CREDENTIAL_CONFIG* CredentialConfig
        );
};

struct MsH3pUniDirStream;
struct MsH3pBiDirStream;

struct MsH3pConnection : public MsQuicConnection {

    MSH3_CONNECTION_CALLBACK_HANDLER Callbacks {nullptr};
    void* Context {nullptr};

    struct lsqpack_enc Encoder;
    struct lsqpack_dec Decoder;
    uint8_t tsu_buf[LSQPACK_LONGEST_SDTC];
    size_t tsu_buf_sz;

    MsH3pUniDirStream* LocalControl {nullptr};
    MsH3pUniDirStream* LocalEncoder {nullptr};
    MsH3pUniDirStream* LocalDecoder {nullptr};

    MsH3pUniDirStream* PeerControl {nullptr};
    MsH3pUniDirStream* PeerEncoder {nullptr};
    MsH3pUniDirStream* PeerDecoder {nullptr};

    uint32_t PeerMaxTableSize {H3_RFC_DEFAULT_HEADER_TABLE_SIZE};
    uint64_t PeerQPackBlockedStreams {H3_RFC_DEFAULT_QPACK_BLOCKED_STREAM};

    std::mutex ShutdownCompleteMutex;
    std::condition_variable ShutdownCompleteEvent;
    bool ShutdownComplete {false};
    bool HandshakeSuccess {false};

    bool DynamicQPackEnabled {false};

    char HostName[256];

    MsH3pConnection(
        const MsQuicRegistration& Registration,
        const MSH3_CONNECTION_CALLBACK_HANDLER Handler,
        void* Context
        );

    MsH3pConnection(
        HQUIC ServerHandle
        );

    ~MsH3pConnection();

    void
    SetCallbackHandler(
        const MSH3_CONNECTION_CALLBACK_HANDLER Handler,
        void* _Context
        )
    {
        Callbacks = Handler;
        Context = _Context;
    }

    MSH3_STATUS
    SetConfigurationH3(
        const MsH3pConfiguration& Configuration
        );

    MSH3_STATUS
    StartH3(
        const MsH3pConfiguration& Configuration,
        const char* ServerName,
        const MSH3_ADDR* ServerAddress
        );

    void WaitOnShutdownComplete() {
        std::unique_lock Lock{ShutdownCompleteMutex};
        ShutdownCompleteEvent.wait(Lock, [&]{return ShutdownComplete;});
    }

private:

    MSH3_STATUS InitializeConfig(const MsH3pConfiguration& Configuration);

    void SetShutdownComplete() {
        std::lock_guard Lock{ShutdownCompleteMutex};
        ShutdownComplete = true;
        ShutdownCompleteEvent.notify_all();
    }

    friend struct MsH3pUniDirStream;
    friend struct MsH3pBiDirStream;

    static QUIC_STATUS
    s_MsQuicCallback(
        _In_ MsQuicConnection* /* Connection */,
        _In_opt_ void* Context,
        _Inout_ QUIC_CONNECTION_EVENT* Event
        )
    {
        return ((MsH3pConnection*)Context)->MsQuicCallback(Event);
    }

    QUIC_STATUS
    MsQuicCallback(
        _Inout_ QUIC_CONNECTION_EVENT* Event
        );

    bool
    ReceiveSettingsFrame(
        _In_ uint32_t BufferLength,
        _In_reads_bytes_(BufferLength)
            const uint8_t * const Buffer
        );
};

struct MsH3pUniDirStream : public MsQuicStream {

    MsH3pConnection& H3;
    H3StreamType Type;

    uint8_t RawBuffer[256];
    QUIC_BUFFER Buffer {0, RawBuffer}; // Working space

    MsH3pUniDirStream(MsH3pConnection& Connection, H3StreamType Type);
    MsH3pUniDirStream(MsH3pConnection& Connection, const MsH3pConfiguration& Configuration); // Type == H3StreamTypeControl
    MsH3pUniDirStream(MsH3pConnection& Connection, const HQUIC StreamHandle);

    // Encoder functions

    bool
    EncodeHeaders(
        _In_ struct MsH3pBiDirStream* Request,
        _In_reads_(HeadersCount)
            const MSH3_HEADER* Headers,
        _In_ size_t HeadersCount
        );

    // Decoder functions

    void
    SendQPackAcknowledgment(
        _In_ uint64_t StreamId
        );

    void
    SendQPackStreamInstructions();

    void
    SendStreamCancellation(
        _In_ uint64_t StreamId
        );

private:

    static QUIC_STATUS
    s_MsQuicCallback(
        _In_ MsQuicStream* /* Stream */,
        _In_opt_ void* Context,
        _Inout_ QUIC_STREAM_EVENT* Event
        )
    {
        auto This = (MsH3pUniDirStream*)Context;
        switch (This->Type) {
        case H3StreamTypeControl:
            return This->ControlStreamCallback(Event);
        case H3StreamTypeEncoder:
            return This->EncoderStreamCallback(Event);
        case H3StreamTypeDecoder:
            return This->DecoderStreamCallback(Event);
        default:
            return This->UnknownStreamCallback(Event);
        }
    }

    QUIC_STATUS
    ControlStreamCallback(
        _Inout_ QUIC_STREAM_EVENT* Event
        );

    void
    ControlReceive(
        _In_ const QUIC_BUFFER* Buffer
        );

    QUIC_STATUS
    EncoderStreamCallback(
        _Inout_ QUIC_STREAM_EVENT* Event
        );

    QUIC_STATUS
    DecoderStreamCallback(
        _Inout_ QUIC_STREAM_EVENT* Event
        );

    QUIC_STATUS
    UnknownStreamCallback(
        _Inout_ QUIC_STREAM_EVENT* Event
        );
};

struct MsH3pAppSend {
    void* AppContext;
    uint8_t FrameHeaderBuffer[16];
    QUIC_BUFFER Buffers[2] = {
        0, FrameHeaderBuffer,
        0, NULL
    };
    MsH3pAppSend(_In_opt_ void* AppContext) : AppContext(AppContext) { }
    bool SetData(
        _In_reads_bytes_opt_(DataLength) const void* Data,
        _In_ uint32_t DataLength
        )
    {
        Buffers[1].Length = DataLength;
        Buffers[1].Buffer = (uint8_t*)Data;
        return H3WriteFrameHeader(H3FrameData, DataLength, &Buffers[0].Length, sizeof(FrameHeaderBuffer), FrameHeaderBuffer);
    }
};

struct MsH3pBiDirStream : public MsQuicStream {

    MsH3pConnection& H3;

    MSH3_REQUEST_CALLBACK_HANDLER Callbacks;
    void* Context;

    uint8_t FrameHeaderBuffer[16];
    uint8_t PrefixBuffer[32];
    uint8_t HeadersBuffer[256];
    QUIC_BUFFER Buffers[3] = { // TODO - Put in AppSend struct?
        {0, FrameHeaderBuffer},
        {0, PrefixBuffer},
        {0, HeadersBuffer}
    };

    static struct lsqpack_dec_hset_if hset_if;
    struct lsxpack_header CurDecodeHeader;
    char DecodeBuffer[4096];

    QUIC_VAR_INT CurFrameType {0};
    QUIC_VAR_INT CurFrameLength {0};
    QUIC_VAR_INT CurFrameLengthLeft {0};
    uint64_t CurRecvCompleteLength {0};
    uint32_t CurRecvOffset {0};

    uint8_t BufferedHeaders[2*sizeof(uint64_t)];
    uint32_t BufferedHeadersLength {0};

    bool Complete {false};
    bool ShutdownComplete {false};
    bool ReceivePending {false};

    MsH3pBiDirStream(
        _In_ MsH3pConnection& Connection,
        const MSH3_REQUEST_CALLBACK_HANDLER Handler,
        _In_ void* Context,
        _In_ MSH3_REQUEST_FLAGS Flags
        ) : MsQuicStream(Connection, ToQuicOpenFlags(Flags), CleanUpManual, s_MsQuicCallback, this),
            H3(Connection), Callbacks(Handler), Context(Context) {
            Start();
        }

    MsH3pBiDirStream(
        _In_ MsH3pConnection& Connection,
        _In_ HQUIC StreamHandle
        ) : MsQuicStream(StreamHandle, CleanUpManual, s_MsQuicCallback, this),
            H3(Connection) { }

    void
    CompleteReceive(
        _In_ uint32_t Length
        );

    bool
    Send(
        _In_ MSH3_REQUEST_SEND_FLAGS Flags,
        _In_reads_(HeadersCount)
            const MSH3_HEADER* Headers,
        _In_ size_t HeadersCount,
        _In_reads_bytes_(DataLength) const void* Data,
        _In_ uint32_t DataLength,
        _In_opt_ void* AppContext
        );

    void
    SetCallbackHandler(
        const MSH3_REQUEST_CALLBACK_HANDLER Handler,
        void* _Context
        )
    {
        Callbacks = Handler;
        Context = _Context;
    }

private:

    QUIC_STATUS
    Receive(
        _Inout_ QUIC_STREAM_EVENT* Event
        );

    static QUIC_STATUS
    s_MsQuicCallback(
        _In_ MsQuicStream* /* Stream */,
        _In_opt_ void* Context,
        _Inout_ QUIC_STREAM_EVENT* Event
        )
    {
        return ((MsH3pBiDirStream*)Context)->MsQuicCallback(Event);
    }

    QUIC_STATUS
    MsQuicCallback(
        _Inout_ QUIC_STREAM_EVENT* Event
        );

    static void
    s_DecodeUnblocked(
        void* /* Context */
        )
    {
        /* no-op currently */
    }

    static struct lsxpack_header*
    s_DecodePrepare(
        void *Context,
        struct lsxpack_header* Header,
        size_t Space
        )
    {
        return ((MsH3pBiDirStream*)Context)->DecodePrepare(Header, Space);
    }

    struct lsxpack_header*
    DecodePrepare(
        struct lsxpack_header* Header,
        size_t Space
        );

    static int
    s_DecodeProcess(
        void *Context,
        struct lsxpack_header* Header
        )
    {
        ((MsH3pBiDirStream*)Context)->DecodeProcess(Header);
        return 0;
    }

    void
    DecodeProcess(
        struct lsxpack_header* Header
        );
};

struct MsH3pListener : public MsQuicListener {

    MSH3_LISTENER_CALLBACK_HANDLER Callbacks;
    void* Context;

    MsH3pListener(
        const MsQuicRegistration& Registration,
        const MSH3_ADDR* Address,
        const MSH3_LISTENER_CALLBACK_HANDLER Handler,
        void* Context
        );

private:

    static
    _IRQL_requires_max_(PASSIVE_LEVEL)
    _Function_class_(QUIC_LISTENER_CALLBACK)
    QUIC_STATUS
    QUIC_API
    s_MsQuicCallback(
        _In_ MsQuicListener* /* Listener */,
        _In_opt_ void* Context,
        _Inout_ QUIC_LISTENER_EVENT* Event
        )
    {
        return ((MsH3pListener*)Context)->MsQuicCallback(Event);
    }

    QUIC_STATUS
    MsQuicCallback(
        _Inout_ QUIC_LISTENER_EVENT* Event
        );
};
