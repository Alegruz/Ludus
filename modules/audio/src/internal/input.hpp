#pragma once
#include <ludus/audio/audio_source.h>

#include "miniaudio.h"
namespace ludus::audio::internal
{
inline ma_result ReadInput(ma_decoder* decoder, void* output, usize size, usize* count) noexcept
{
    usize read = 0;
    auto* input = static_cast<StreamInput*>(decoder->pUserData);
    const auto status = input->Read({static_cast<uint8*>(output), size}, read);
    *count = read;
    return status == Status::Ok ? (read == 0 ? MA_AT_END : MA_SUCCESS) : MA_IO_ERROR;
}
inline ma_result SeekInput(ma_decoder* decoder, ma_int64 offset, ma_seek_origin origin) noexcept
{
    auto* input = static_cast<StreamInput*>(decoder->pUserData);
    return input->Seek(offset, origin == ma_seek_origin_current) == Status::Ok ? MA_SUCCESS : MA_IO_ERROR;
}
inline ma_decoder_config InputConfig(SourceFormat format, uint32 channels, uint32 rate) noexcept
{
    auto config = ma_decoder_config_init(ma_format_f32, channels, rate);
    config.encodingFormat = format == SourceFormat::Wav ? ma_encoding_format_wav : ma_encoding_format_flac;
    return config;
}
} // namespace ludus::audio::internal
