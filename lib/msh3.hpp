/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#include <msquic.hpp>
#include <lsqpack.h>

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

#include <quic_var_int.h>

enum H3SettingsType {
    H3SettingQPackMaxTableCapacity,
    H3SettingMaxHeaderListSize,
    H3SettingQPackBlockedStreamsSize,
    H3SettingNumPlaceholders,
    H3SettingMax
};

struct H3HeadingPair {
    char* Name;
    char* Value;
    uint32_t NameLength;
    uint32_t ValueLength;
};

struct H3Headers {
    H3HeadingPair* Pairs;
    uint32_t PairCount;
};

struct H3Settings {
    H3SettingsType Type;
    uint64_t Integer;
};

enum H3_FRAME_TYPE {
    H3FrameData,
    H3FrameHeaders,
    H3FrameSettings,
    H3FrameGoaway,
    H3FrameUnknown,
};

#define H3_STREAM_TYPE_CONTROL 0x00
#define H3_STREAM_TYPE_PUSH    0x01
#define H3_STREAM_TYPE_ENCODER 0x02
#define H3_STREAM_TYPE_DECODER 0x03
#define H3_STREAM_TYPE_UNKNOWN 0xFF

#define H3_FRAME_TYPE_DATA           0x0
#define H3_FRAME_TYPE_HEADERS        0x1
#define H3_FRAME_TYPE_PRIORITY       0x2
#define H3_FRAME_TYPE_CANCEL_PUSH    0x3
#define H3_FRAME_TYPE_SETTINGS       0x4
#define H3_FRAME_TYPE_PUSH_PROMISE   0x5
#define H3_FRAME_TYPE_RESERVED1      0x6
#define H3_FRAME_TYPE_GOAWAY         0x7
#define H3_FRAME_TYPE_RESERVED2      0x8
#define H3_FRAME_TYPE_RESERVED3      0x9

#define H3_MAX_VARIABLE_LENGTH_INTEGER_SIZE 8ul
#define H3_MAX_FRAME_HEADER_SIZE (H3_MAX_VARIABLE_LENGTH_INTEGER_SIZE + H3_MAX_VARIABLE_LENGTH_INTEGER_SIZE)
#define H3_MAX_INTEGER_SETTINGS_SIZE (H3_MAX_VARIABLE_LENGTH_INTEGER_SIZE + H3_MAX_VARIABLE_LENGTH_INTEGER_SIZE)

//
// Setting IDs
//

#define H3_SETTING_QPACK_MAX_TABLE_CAPACITY 0x1
#define H3_QUIC_SETTING_RESERVED1           0x2
#define H3_SETTING_RESERVED2                0x3
#define H3_SETTING_RESERVED3                0x4
#define H3_SETTING_RESERVED4                0x5
#define H3_SETTING_MAX_HEADER_LIST_SIZE     0x6
#define H3_SETTING_QPACK_BLOCKED_STREAMS    0x7
#define H3_SETTING_RESERVED5                0x8
#define H3_SETTING_NUM_PLACEHOLDERS         0x9

static const uint16_t H3KnownSettingsMap[H3SettingMax] = {
    H3_SETTING_QPACK_MAX_TABLE_CAPACITY,
    H3_SETTING_MAX_HEADER_LIST_SIZE,
    H3_SETTING_QPACK_BLOCKED_STREAMS,
    H3_SETTING_NUM_PLACEHOLDERS,
};

#define H3_RFC_DEFAULT_HEADER_TABLE_SIZE    0
#define H3_DEFAULT_MAX_HEADER_LIST_SIZE H3_SETTING_MAX_SIZE
#define H3_RFC_DEFAULT_QPACK_BLOCKED_STREAM 0

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
        PayloadSize += QuicVarIntSize(H3KnownSettingsMap[Settings[i].Type]);
        PayloadSize += QuicVarIntSize(Settings[i].Integer);
    }
    if (!H3WriteFrameHeader(
            H3_FRAME_TYPE_SETTINGS,
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
        Buffer = QuicVarIntEncode(H3KnownSettingsMap[Settings[i].Type], Buffer);
        Buffer = QuicVarIntEncode(Settings[i].Integer, Buffer);
    }
    *Offset += PayloadSize;
    return true;
}

struct MsH3UniDirStream;

struct MsH3Connection : public MsQuicConnection {

    struct lsqpack_enc QPack;

    MsH3UniDirStream* LocalControl {nullptr};
    MsH3UniDirStream* LocalEncoder {nullptr};
    MsH3UniDirStream* LocalDecoder {nullptr};

    MsH3UniDirStream* PeerControl {nullptr};
    MsH3UniDirStream* PeerEncoder {nullptr};
    MsH3UniDirStream* PeerDecoder {nullptr};

    uint8_t RawSettingsBuffer[64];
    QUIC_BUFFER SettingsBuffer;

    uint8_t EncoderStreamType {H3_STREAM_TYPE_ENCODER};
    QUIC_BUFFER EncoderStreamTypeBuffer = {sizeof(EncoderStreamType), &EncoderStreamType};

    uint8_t DecoderStreamType {H3_STREAM_TYPE_DECODER};
    QUIC_BUFFER DecoderStreamTypeBuffer = {sizeof(DecoderStreamType), &DecoderStreamType};

    MsH3Connection(const MsQuicRegistration& Registration);
    ~MsH3Connection();

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

    void CreateLocalStreams();
};

enum H3StreamType {
    H3StreamTypeUnknown,
    H3StreamTypeControl,
    H3StreamTypeEncoder,
    H3StreamTypeDecoder,
};

struct MsH3UniDirStream : public MsQuicStream {

    MsH3Connection& H3;
    H3StreamType Type;

    MsH3UniDirStream(MsH3Connection* Connection, H3StreamType Type, QUIC_STREAM_OPEN_FLAGS Flags = QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL | QUIC_STREAM_OPEN_FLAG_0_RTT);
    MsH3UniDirStream(MsH3Connection* Connection, const HQUIC StreamHandle);

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
