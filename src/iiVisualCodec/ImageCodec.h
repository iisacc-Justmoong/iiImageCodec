#pragma once
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace ii::visual::image {
enum class ErrorCode { invalid_input, unsupported_format, limit_exceeded, codec_failure, cancelled };
struct Error final { ErrorCode code; std::string message; };
template<class T> using Result = std::expected<T, Error>;
enum class PixelFormat { rgb8, rgba8 };
enum class Format { png, jpeg, bmp, gif, tga, psd, webp, tiff, unknown };
struct Metadata final {
    std::vector<std::uint8_t> icc_profile;
    std::uint32_t dots_per_meter_x=0,dots_per_meter_y=0;
    unsigned orientation=1;
};
struct Limits final {
    std::size_t input_bytes = 64 * 1024 * 1024;
    std::size_t output_bytes = 64 * 1024 * 1024;
    std::size_t pixel_bytes = 256 * 1024 * 1024;
    std::uint32_t dimension = 32768;
};
struct ImageView final {
    std::uint32_t width = 0, height = 0;
    PixelFormat format = PixelFormat::rgba8;
    std::size_t row_stride = 0; // zero means tightly packed
    std::span<const std::uint8_t> pixels;
    const Metadata* metadata=nullptr;
};
struct Image final {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba; // tightly packed, straight alpha, top-to-bottom
    Metadata metadata;
    [[nodiscard]] ImageView view() const noexcept { return {width, height, PixelFormat::rgba8, 0, rgba,&metadata}; }
};
struct Info final { Format format; std::uint32_t width, height; };
// Codecs operate on memory only; publication belongs to iiFileProvider.
[[nodiscard]] Result<Info> inspect(std::span<const std::uint8_t>, Limits = {});
[[nodiscard]] Result<Image> decode(std::span<const std::uint8_t>, Limits = {});
// Explicit, memory-only ICC conversion. Decoding itself preserves source color
// values/profile; document consumers opt into their sRGB working space here.
[[nodiscard]] Result<Image> to_srgb(Image, Limits = {});
// Center-cropped square with bilinear, premultiplied-alpha filtering. Metadata
// is retained; orientation must already be normalized (decode() does this).
[[nodiscard]] Result<Image> thumbnail(ImageView, std::uint32_t edge, bool circular=true, Limits = {});
[[nodiscard]] Result<std::vector<std::uint8_t>> encode_png(ImageView, Limits = {});
// JPEG accepts RGB only. Alpha compositing and metadata policy belong to callers.
[[nodiscard]] Result<std::vector<std::uint8_t>> encode_jpeg(ImageView, int quality = 90, Limits = {});
struct EncodeOptions final {int quality=90;std::uint32_t matte_rgb=0xffffff;};
// PNG/BMP/TGA/WebP/TIFF preserve straight alpha; JPEG explicitly flattens it.
// GIF/PSD are read-only formats here; layered PSD belongs to the document SDK.
[[nodiscard]] Result<std::vector<std::uint8_t>> encode(ImageView,Format,EncodeOptions={},Limits={});
}

// Source compatibility for existing iiSDK consumers.
namespace ii { namespace image = visual::image; }
