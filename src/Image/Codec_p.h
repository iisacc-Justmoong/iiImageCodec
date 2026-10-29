#pragma once
#include <iiVisualCodec/ImageCodec.h>
namespace ii::visual::image::detail {
Result<Info> inspect_basic(std::span<const std::uint8_t>,Limits);
Result<Image> decode_basic(std::span<const std::uint8_t>,Limits);
Result<Image> decode_metadata(std::span<const std::uint8_t>,Image,Format);
Result<std::vector<std::uint8_t>> encode_metadata(std::vector<std::uint8_t>,Format,const Metadata*,Limits);
}
