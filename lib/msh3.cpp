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

    MsQuicAlpn Alpns("h3", "h3-29");
    MsQuicSettings Settings;
    Settings.SetSendBufferingEnabled(false);
    Settings.SetPeerBidiStreamCount(1000);
    Settings.SetPeerUnidiStreamCount(3);
    Settings.SetIdleTimeoutMs(5000);
    auto Flags =
        Unsecure ?
            QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION :
            QUIC_CREDENTIAL_FLAG_CLIENT;
    MsQuicCredentialConfig Creds(Flags);
    MsQuicConfiguration Config(Reg, Alpns, Settings, Creds);
    if (QUIC_FAILED(Config.GetInitStatus())) return false;

    MsH3Connection H3(Reg);
    if (QUIC_FAILED(H3.GetInitStatus())) return false;

    //if (ServerIp) ASSERT_SUCCESS(H3.SetRemoteAddr(ServerAddress));
    if (QUIC_FAILED(H3.Start(Config, ServerName, 443))) return false;

    const H3Settings SettingsH3[] = {
        { H3SettingQPackMaxTableCapacity, 4096 },
        { H3SettingQPackBlockedStreamsSize, 100 },
    };
    uint8_t RawSettingsBuffer[64];
    QUIC_BUFFER SettingsBuffer;
    SettingsBuffer.Buffer = RawSettingsBuffer;
    SettingsBuffer.Buffer[0] = H3_STREAM_TYPE_CONTROL;
    SettingsBuffer.Length = 1;
    if (!H3WriteSettingsFrame(SettingsH3, ARRAYSIZE(SettingsH3), &SettingsBuffer.Length, sizeof(RawSettingsBuffer), RawSettingsBuffer)) {
        return false;
    }
    MsH3UniDirStream Control(H3, H3StreamTypeControl);
    if (QUIC_FAILED(Control.GetInitStatus())) return false;
    if (QUIC_FAILED(Control.Send(&SettingsBuffer, 1, QUIC_SEND_FLAG_ALLOW_0_RTT | QUIC_SEND_FLAG_START))) return false;

    MsH3UniDirStream Encoder(H3, H3StreamTypeEncoder);
    if (QUIC_FAILED(Encoder.GetInitStatus())) return false;
    uint8_t EncoderStreamType = H3_STREAM_TYPE_ENCODER;
    QUIC_BUFFER EncoderStreamTypeBuffer = {sizeof(EncoderStreamType), &EncoderStreamType};
    if (QUIC_FAILED(Encoder.Send(&EncoderStreamTypeBuffer, 1, QUIC_SEND_FLAG_ALLOW_0_RTT | QUIC_SEND_FLAG_START))) return false;

    MsH3UniDirStream Decoder(H3, H3StreamTypeDecoder);
    if (QUIC_FAILED(Decoder.GetInitStatus())) return false;
    uint8_t DecoderStreamType = H3_STREAM_TYPE_DECODER;
    QUIC_BUFFER DecoderStreamTypeBuffer = {sizeof(DecoderStreamType), &DecoderStreamType};
    if (QUIC_FAILED(Decoder.Send(&DecoderStreamTypeBuffer, 1, QUIC_SEND_FLAG_ALLOW_0_RTT | QUIC_SEND_FLAG_START))) return false;

    std::this_thread::sleep_for(std::chrono::seconds(10));

    if (!Path) return false;

    return true;
}
