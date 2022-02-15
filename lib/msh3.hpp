/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include <msquic.hpp>
#include <lsqpack.h>
#include <lsxpack_header.h>
#include <vector>

#if _WIN32
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

#include <quic_var_int.h>

enum H3SettingsType {
    H3SettingQPackMaxTableCapacity = 1,
    H3SettingMaxHeaderListSize = 6,
    H3SettingQPackBlockedStreamsSize = 7,
    H3SettingNumPlaceholders = 9,
};

// Contiguous buffer for (non-null-terminated) header name and value strings.
struct H3HeadingPair : public lsxpack_header_t {
    char Buffer[64] = {0};
    bool Set(_In_z_ const char* Name, _In_z_ const char* Value) {
        if (strlen(Name) + strlen(Value) > sizeof(Buffer)) return false;
        buf = Buffer;
        name_offset = 0;
        name_len = (lsxpack_strlen_t)strlen(Name);
        val_offset = name_len;
        val_len = (lsxpack_strlen_t)strlen(Value);
        memcpy(Buffer, Name, name_len);
        memcpy(Buffer+name_len, Value, val_len);
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
    H3StreamTypePush,
    H3StreamTypeEncoder,
    H3StreamTypeDecoder,
};

enum H3FrameType {
    H3FrameData,
    H3FrameHeaders,
    H3FramePriority,
    H3FrameCancelPush,
    H3FrameSettings,
    H3FramePushPromise,
    H3FrameGoaway = 7,
};

#define H3_RFC_DEFAULT_HEADER_TABLE_SIZE    0
#define H3_RFC_DEFAULT_QPACK_BLOCKED_STREAM 0
#define H3_DEFAULT_QPACK_MAX_TABLE_CAPACITY 4096
#define H3_DEFAULT_QPACK_BLOCKED_STREAMS 100

const H3Settings SettingsH3[] = {
    { H3SettingQPackMaxTableCapacity, H3_DEFAULT_QPACK_MAX_TABLE_CAPACITY },
    { H3SettingQPackBlockedStreamsSize, H3_DEFAULT_QPACK_BLOCKED_STREAMS },
};

inline
bool
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

inline
bool
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

struct MsH3UniDirStream;
struct MsH3BiDirStream;

struct MsH3Connection : public MsQuicConnection {

    struct lsqpack_enc QPack;
    uint8_t tsu_buf[LSQPACK_LONGEST_SDTC];
    size_t tsu_buf_sz;

    MsH3UniDirStream* LocalControl {nullptr};
    MsH3UniDirStream* LocalEncoder {nullptr};
    MsH3UniDirStream* LocalDecoder {nullptr};

    MsH3UniDirStream* PeerControl {nullptr};
    MsH3UniDirStream* PeerEncoder {nullptr};
    MsH3UniDirStream* PeerDecoder {nullptr};

    uint32_t PeerMaxTableSize {H3_RFC_DEFAULT_HEADER_TABLE_SIZE};
    uint64_t PeerQPackBlockedStreams {H3_RFC_DEFAULT_QPACK_BLOCKED_STREAM};

    std::vector<MsH3BiDirStream*> Requests;

    MsH3Connection(const MsQuicRegistration& Registration);
    ~MsH3Connection();

    bool
    SendRequest(
        _In_z_ const char* Method,
        _In_z_ const char* Host,
        _In_z_ const char* Path
        );

private:

    friend struct MsH3UniDirStream;
    friend struct MsH3BiDirStream;

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
        );

    bool
    ReceiveSettingsFrame(
        _In_ uint32_t BufferLength,
        _In_reads_bytes_(BufferLength)
            const uint8_t * const Buffer
        );
};

struct MsH3UniDirStream : public MsQuicStream {

    MsH3Connection& H3;
    H3StreamType Type;

    uint8_t RawBuffer[256];
    QUIC_BUFFER Buffer {0, RawBuffer}; // Working space

    MsH3UniDirStream(MsH3Connection* Connection, H3StreamType Type, QUIC_STREAM_OPEN_FLAGS Flags = QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL | QUIC_STREAM_OPEN_FLAG_0_RTT);
    MsH3UniDirStream(MsH3Connection* Connection, const HQUIC StreamHandle);

    bool EncodeHeaders(_In_ struct MsH3BiDirStream* Request);

private:

    static
    QUIC_STATUS
    s_MsQuicCallback(
        _In_ MsQuicStream* /* Stream */,
        _In_opt_ void* Context,
        _Inout_ QUIC_STREAM_EVENT* Event
        )
    {
        auto This = (MsH3UniDirStream*)Context;
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

struct MsH3BiDirStream : public MsQuicStream {

    MsH3Connection& H3;

    H3HeadingPair Headers[4];

    uint8_t FrameHeaderBuffer[16];
    uint8_t PrefixBuffer[32];
    uint8_t HeadersBuffer[256];
    QUIC_BUFFER Buffers[3] = {
        {0, FrameHeaderBuffer},
        {0, PrefixBuffer},
        {0, HeadersBuffer}
    };

    MsH3BiDirStream(
        _In_ MsH3Connection* Connection,
        _In_z_ const char* Method,
        _In_z_ const char* Host,
        _In_z_ const char* Path,
        _In_ QUIC_STREAM_OPEN_FLAGS Flags = QUIC_STREAM_OPEN_FLAG_0_RTT
        );

private:

    void
    Receive(
        _In_ const QUIC_BUFFER* Buffer
        );

    static
    QUIC_STATUS
    s_MsQuicCallback(
        _In_ MsQuicStream* /* Stream */,
        _In_opt_ void* Context,
        _Inout_ QUIC_STREAM_EVENT* Event
        )
    {
        return ((MsH3BiDirStream*)Context)->MsQuicCallback(Event);
    }

    QUIC_STATUS
    MsQuicCallback(
        _Inout_ QUIC_STREAM_EVENT* Event
        );
};
