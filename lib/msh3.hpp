/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

--*/

#pragma once

#include <msquic.hpp>
#include <lsqpack.h>
#include <lsxpack_header.h>
#include <stdio.h>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "msh3.h"

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
    H3FrameUnknown = 0xFF
};

#define H3_RFC_DEFAULT_HEADER_TABLE_SIZE    0
#define H3_RFC_DEFAULT_QPACK_BLOCKED_STREAM 0
#define H3_DEFAULT_QPACK_MAX_TABLE_CAPACITY 0
#define H3_DEFAULT_QPACK_BLOCKED_STREAMS 0

const H3Settings SettingsH3[] = {
    //{ H3SettingQPackMaxTableCapacity, H3_DEFAULT_QPACK_MAX_TABLE_CAPACITY },
    { H3SettingQPackBlockedStreamsSize, H3_DEFAULT_QPACK_BLOCKED_STREAMS },
};

// Copied from QuicVanIntDecode and changed to uint32_t offset/length
inline
_Success_(return != FALSE)
BOOLEAN
MsH3VarIntDecode(
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

    struct lsqpack_enc Encoder;
    struct lsqpack_dec Decoder;
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

    std::mutex HandshakeCompleteMutex;
    std::condition_variable HandshakeCompleteEvent;
    bool HandshakeSuccess {false};

    std::mutex ShutdownCompleteMutex;
    std::condition_variable ShutdownCompleteEvent;
    bool ShutdownComplete {false};

    MsH3Connection(const MsQuicRegistration& Registration);
    ~MsH3Connection();

    bool
    SendRequest(
        _In_ const MSH3_REQUEST_IF* Interface,
        _In_ void* IfContext,
        _In_z_ const char* Method,
        _In_z_ const char* Host,
        _In_z_ const char* Path
        );

    bool WaitOnHandshakeComplete() {
        if (!HandshakeComplete) {
            std::unique_lock Lock{HandshakeCompleteMutex};
            HandshakeCompleteEvent.wait(Lock, [&]{return HandshakeComplete;});
        }
        return HandshakeSuccess;
    }

    void WaitOnShutdownComplete() {
        std::unique_lock Lock{ShutdownCompleteMutex};
        ShutdownCompleteEvent.wait(Lock, [&]{return ShutdownComplete;});
    }

private:

    void SetHandshakeComplete() {
        std::lock_guard Lock{HandshakeCompleteMutex};
        HandshakeComplete = true;
        HandshakeCompleteEvent.notify_all();
    }

    void SetShutdownComplete() {
        std::lock_guard Lock{ShutdownCompleteMutex};
        ShutdownComplete = true;
        ShutdownCompleteEvent.notify_all();
    }

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

    MSH3_REQUEST_IF Callbacks;
    void* Context;

    H3HeadingPair Headers[4];

    uint8_t FrameHeaderBuffer[16];
    uint8_t PrefixBuffer[32];
    uint8_t HeadersBuffer[256];
    QUIC_BUFFER Buffers[3] = {
        {0, FrameHeaderBuffer},
        {0, PrefixBuffer},
        {0, HeadersBuffer}
    };

    static struct lsqpack_dec_hset_if hset_if;
    struct lsxpack_header CurDecodeHeader;
    char DecodeBuffer[512];

    H3FrameType CurFrameType {H3FrameUnknown};
    uint32_t CurFrameLength {0};

    bool Complete {false};

    MsH3BiDirStream(
        _In_ MsH3Connection* Connection,
        _In_ const MSH3_REQUEST_IF* Interface,
        _In_ void* IfContext,
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

    static
    void
    s_DecodeUnblocked(
        void *Context
        )
    {
        ((MsH3BiDirStream*)Context)->DecodeUnblocked();
    }

    void DecodeUnblocked();

    static
    struct lsxpack_header*
    s_DecodePrepare(
        void *Context,
        struct lsxpack_header* Header,
        size_t Space
        )
    {
        return ((MsH3BiDirStream*)Context)->DecodePrepare(Header, Space);
    }

    struct lsxpack_header*
    DecodePrepare(
        struct lsxpack_header* Header,
        size_t Space
        );

    static
    int
    s_DecodeProcess(
        void *Context,
        struct lsxpack_header* Header
        )
    {
        return ((MsH3BiDirStream*)Context)->DecodeProcess(Header);
    }

    int
    DecodeProcess(
        struct lsxpack_header* Header
        );
};
