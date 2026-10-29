#pragma once
#include "ImageCodec.h"
#include <algorithm>
#include <limits>

namespace ii::visual::detail {
inline auto error(image::ErrorCode code, std::string message) {
    return std::unexpected(image::Error{code, std::move(message)});
}
inline image::Result<std::size_t> stride(image::ImageView view, image::Limits limits) {
    if (view.format != image::PixelFormat::rgb8 && view.format != image::PixelFormat::rgba8)
        return error(image::ErrorCode::invalid_input, "Invalid pixel format");
    if (!view.width || !view.height)
        return error(image::ErrorCode::invalid_input, "Empty image dimensions");
    if (view.width > limits.dimension || view.height > limits.dimension ||
        std::uint64_t(view.width) * view.height > limits.pixel_bytes / 4)
        return error(image::ErrorCode::limit_exceeded, "Image exceeds dimensions or pixel budget");
    const std::size_t channels = view.format == image::PixelFormat::rgb8 ? 3 : 4;
    const auto row = std::size_t(view.width) * channels;
    const auto step = view.row_stride ? view.row_stride : row;
    if (step < row || (view.height > 1 && step > (std::numeric_limits<std::size_t>::max() - row) / (view.height - 1)) ||
        (view.height - 1) * step + row > view.pixels.size())
        return error(image::ErrorCode::invalid_input, "Invalid image row stride or pixel extent");
    return step;
}
inline image::Image rgba_copy(image::ImageView view, std::size_t step) {
    image::Image result{view.width, view.height, std::vector<std::uint8_t>(std::size_t(view.width)*view.height*4)};
    if (view.metadata) result.metadata = *view.metadata;
    const unsigned channels = view.format == image::PixelFormat::rgb8 ? 3 : 4;
    for (std::uint32_t y=0;y<view.height;++y) for (std::uint32_t x=0;x<view.width;++x) {
        const auto* from = view.pixels.data() + y*step + x*channels;
        auto* to = result.rgba.data() + (std::size_t(y)*view.width+x)*4;
        std::copy_n(from, 3, to); to[3] = channels==4 ? from[3] : 255;
    }
    return result;
}
}
