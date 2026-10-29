#include <iiVisualCodec/VideoCodec.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace ii::visual;
int failures = 0;
void check(bool condition, std::string_view message)
{
    if (!condition) { std::cerr << message << '\n'; ++failures; }
}
std::vector<std::uint8_t> fixture(const char *name)
{
    const auto path = std::filesystem::path(IIVISUALCODEC_VIDEO_FIXTURE_DIR) / name;
    std::ifstream file(path, std::ios::binary);
    if (!file) { std::cerr << "Cannot open fixture: " << path << '\n'; ++failures; return {}; }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
}
int main()
{
    const auto plain = fixture("display-sar-2x.mp4");
    const auto rotated = fixture("display-matrix-90.mp4");
    const auto tagged = fixture("display-tagged.mkv");
    const auto base = video::inspect_display(plain);
    check(bool(base), "display inspection accepts a bounded MP4 with audio");
    if (base) {
        check(base->video.width == 16 && base->video.height == 8 && base->video.codec == "mpeg4",
              "display inspection retains the coded extent and codec");
        check(base->sample_aspect_ratio.numerator == 2 && base->sample_aspect_ratio.denominator == 1,
              "non-square sample aspect ratio is preserved exactly");
        check(!base->has_display_matrix && base->rotation_degrees == 0.0,
              "an unrotated source has no display matrix");
        check(base->rotation_tag.empty(), "an untagged source has no rotation tag");
        check(base->audio_stream_count == 1 && base->pixel_format == "yuv420p"
              && base->container.find("mp4") != std::string::npos,
              "container, source pixel format and audio streams are reported");
    }
    const auto tag = video::inspect_display(tagged);
    check(tag && !tag->has_display_matrix && tag->rotation_tag == "90",
          "a container rotation tag remains available without guessing a transform");
    auto singular = rotated;
    constexpr std::array<std::uint8_t, 36> matrix{
        0,0,0,0, 0xff,0xff,0,0, 0,0,0,0, 0,1,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0x40,0,0,0};
    auto position = std::search(singular.begin(), singular.end(), matrix.begin(), matrix.end());
    check(position != singular.end(), "rotated fixture contains its display matrix");
    if (position != singular.end()) {
        std::fill(position + 4, position + 8, 0);
        std::fill(position + 12, position + 16, 0);
        const auto invalid = video::inspect_display(singular);
        check(!invalid && invalid.error().code == image::ErrorCode::invalid_input,
              "a singular display matrix is rejected explicitly");
    }
    const auto display = video::inspect_display(rotated);
    check(bool(display), "display inspection accepts the rotated fixture");
    if (display) {
        check(display->has_display_matrix && std::abs(display->rotation_degrees - 90.0) < 0.01,
              "counterclockwise display rotation is preserved");
        check(display->display_matrix[0] == 0 && display->display_matrix[1] == -65536
              && display->display_matrix[3] == 65536 && display->display_matrix[4] == 0
              && display->display_matrix[8] == 1073741824,
              "the complete fixed-point display matrix is available to the caller");
        check(display->sample_aspect_ratio.numerator == 2 && display->sample_aspect_ratio.denominator == 1
              && display->audio_stream_count == 1,
              "rotation does not discard sample aspect ratio or audio metadata");
        const auto decoded = video::decode(rotated);
        check(decoded && decoded->info.width == 16 && decoded->info.height == 8 && decoded->frames.size() == 1,
              "existing decode keeps coded pixels and dimensions unchanged");
        const auto timed = video::decode_with_timing(rotated);
        check(timed && decoded && !timed->used_fallback_timestamps && !timed->frame_display_matrix_differs
              && timed->video.frames.size() == decoded->frames.size()
              && timed->video.frames[0].pts == decoded->frames[0].pts
              && timed->video.frames[0].image.rgba == decoded->frames[0].image.rgba,
              "timing decode reports timestamp provenance without changing legacy pixels or timestamps");
        int cancellation_checks = 0;
        const auto cancelled = video::decode_with_timing(rotated, {}, [&] { return ++cancellation_checks >= 4; });
        check(!cancelled && cancelled.error().code == image::ErrorCode::cancelled && cancellation_checks >= 4,
              "timing decode checks cancellation while processing packets or frames without returning partial output");
        const auto cancelled_before = video::decode_with_timing(rotated, {}, [] { return true; });
        check(!cancelled_before && cancelled_before.error().code == image::ErrorCode::cancelled,
              "timing decode can be cancelled before opening the input");
        const auto callback_error = video::decode_with_timing(rotated, {}, []() -> bool {
            throw std::runtime_error("cancellation callback failed");
        });
        check(!callback_error && callback_error.error().code == image::ErrorCode::codec_failure,
              "an exception from the cancellation callback becomes a codec error");
    }
    const auto frame_only = fixture("display-frame-sei-90.mkv");
    const auto frame_display = video::inspect_display(frame_only);
    check(frame_display && !frame_display->has_display_matrix && frame_display->rotation_tag.empty(),
          "the frame-only fixture has no stream-level display transform");
    const auto frame_timing = video::decode_with_timing(frame_only);
    check(frame_timing && frame_timing->frame_display_matrix_differs,
          "a decoded frame's display matrix differing from the stream is reported");
    video::Limits limits; limits.input_bytes = 1;
    const auto limited = video::inspect_display(rotated, limits);
    check(!limited && limited.error().code == image::ErrorCode::limit_exceeded,
          "display inspection honors compressed input limits");
    check(!video::inspect_display(std::vector<std::uint8_t>{1, 2, 3, 4}),
          "malformed input is rejected");
    const auto limited_decode = video::decode_with_timing(rotated, limits);
    check(!limited_decode && limited_decode.error().code == image::ErrorCode::limit_exceeded,
          "timing decode observes the existing compressed input limit");

    image::Image still{16, 8, std::vector<std::uint8_t>(16 * 8 * 4, 255)};
    video::EncodeOptions encode_options; encode_options.time_base = {1, 50};
    for (std::size_t count : {std::size_t(1), std::size_t(2), std::size_t(3)}) {
        std::vector<video::FrameView> frames;
        for (std::size_t index = 0; index < count; ++index)
            frames.push_back({still.view(), std::int64_t(index * 2), 2});
        const auto encoded = video::encode(frames, encode_options);
        check(bool(encoded), "constant-rate FFV1 video can be encoded");
        if (!encoded) continue;
        const auto info = video::inspect(*encoded);
        check(info && info->frame_rate.numerator > 0 && info->frame_rate.denominator > 0
              && std::int64_t(info->frame_rate.numerator) == 25 * std::int64_t(info->frame_rate.denominator),
              "short FFV1 videos expose the rate implied by frame duration, not the muxer tick rate");
        const auto decoded = video::decode(*encoded);
        check(decoded && decoded->frames.size() == count,
              "declaring the constant rate does not discard frames");
    }
    encode_options.time_base = {1001, 30000};
    const std::array fractional{video::FrameView{still.view(), 0, 1},
                                video::FrameView{still.view(), 1, 1}};
    const auto encoded_fractional = video::encode(fractional, encode_options);
    const auto fractional_info = encoded_fractional ? video::inspect(*encoded_fractional)
                                                    : image::Result<video::Info>(std::unexpected(encoded_fractional.error()));
    check(fractional_info && fractional_info->frame_rate.numerator > 0
          && fractional_info->frame_rate.denominator > 0
          && std::int64_t(fractional_info->frame_rate.numerator) * 1001
             == std::int64_t(fractional_info->frame_rate.denominator) * 30000,
          "fractional frame rates survive short constant-rate Matroska videos");
    encode_options.time_base = {1, 50};
    const std::array variable{video::FrameView{still.view(), 0, 2},
                              video::FrameView{still.view(), 3, 2},
                              video::FrameView{still.view(), 8, 2}};
    const auto encoded_variable = video::encode(variable, encode_options);
    const auto decoded_variable = encoded_variable ? video::decode(*encoded_variable)
                                                   : image::Result<video::Video>(std::unexpected(encoded_variable.error()));
    check(decoded_variable && decoded_variable->frames.size() == 3
          && decoded_variable->frames[2].pts - decoded_variable->frames[1].pts
             > decoded_variable->frames[1].pts - decoded_variable->frames[0].pts,
          "variable frame gaps remain variable and are not silently converted to CFR");
    return failures ? 1 : 0;
}
