#pragma once
#include "Spatial.h"
#include <array>
#include <functional>

namespace ii::visual::video {
using image::Result;
struct Rational final { int numerator = 1, denominator = 30; };
enum class Container { matroska, mp4, mov, avi };
enum class Codec { ffv1, mpeg4, mjpeg, h264, hevc, vp9, av1 };
struct Limits final {
    image::Limits image;
    std::size_t input_bytes = 512 * 1024 * 1024;
    std::size_t output_bytes = 512 * 1024 * 1024;
    std::size_t decoded_bytes = 512 * 1024 * 1024;
    std::size_t frames = 4096;
    std::size_t packets = 1000000;
};
struct FrameView final {
    image::ImageView image;
    std::int64_t pts = 0;
    std::int64_t duration = 1;
};
struct Frame final {
    image::Image image;
    std::int64_t pts = 0, duration = 0;
};
struct Info final {
    std::uint32_t width = 0, height = 0;
    Rational time_base, frame_rate;
    std::int64_t duration = 0; // time_base units, 0 when unknown
    std::string codec;
    spatial::Metadata spatial;
};
// Source presentation metadata. Decoded frames retain the coded orientation;
// applications decide how to apply the transform and handle source audio.
struct DisplayInfo final {
    Info video;
    // 0/1 means unspecified; otherwise this is the exact positive sample ratio.
    Rational sample_aspect_ratio{0, 1};
    // ISO/IEC 14496-12 3x3 matrix, native-endian fixed-point components.
    bool has_display_matrix = false;
    std::array<std::int32_t, 9> display_matrix{};
    // Counterclockwise degrees from the matrix; zero when no matrix is present.
    double rotation_degrees = 0;
    // Verbatim stream tag, retained separately rather than guessing its semantics.
    std::string rotation_tag, container, pixel_format;
    std::uint32_t audio_stream_count = 0;
};
struct Video final { Info info; std::vector<Frame> frames; };
struct TimedVideo final {
    Video video;
    // True when this API had to synthesize at least one missing FFmpeg timestamp.
    bool used_fallback_timestamps = false;
    // True when a decoded frame has a malformed display matrix or one that
    // differs from the stream matrix (identity when the stream has none).
    bool frame_display_matrix_differs = false;
};
struct EncodeOptions final {
    Container container = Container::matroska;
    Codec codec = Codec::ffv1;
    Rational time_base;
    std::int64_t bit_rate = 8000000;
    spatial::Metadata spatial;
};
struct BackendInfo final { std::string version, license, configuration; };
[[nodiscard]] BackendInfo backend_info();
// All I/O is bounded memory. External files, URLs and nested playlist access
// are disabled; source audio/subtitle streams are not returned by this API.
[[nodiscard]] Result<Info> inspect(std::span<const std::uint8_t>, Limits = {});
[[nodiscard]] Result<DisplayInfo> inspect_display(std::span<const std::uint8_t>, Limits = {});
[[nodiscard]] Result<Video> decode(std::span<const std::uint8_t>, Limits = {});
// Cancellation is cooperative between bounded probing, packets, and frames;
// it does not interrupt an individual backend call. No partial video is returned.
[[nodiscard]] Result<TimedVideo> decode_with_timing(std::span<const std::uint8_t>, Limits = {},
                                                   const std::function<bool()>& cancelled = {});
[[nodiscard]] Result<std::vector<std::uint8_t>> encode(std::span<const FrameView>,
                                                    EncodeOptions = {}, Limits = {});
}
