#pragma once
// Explicit native interop boundary; ordinary Vision/OpenCV headers use only
// standard C++ values. Link iiVisualCodec::OpenCV before including this header.
#include <iiVisualCodec/OpenCV.h>
#include <opencv2/core/mat.hpp>

namespace ii::visual::opencv {
enum class PixelOrder { rgb, bgr, rgba, bgra, gray };
// Both directions copy into owned storage. Padded image rows and non-contiguous
// cv::Mat ROIs are supported; Mat input must be unsigned 8-bit with matching channels.
[[nodiscard]] Result<cv::Mat> to_mat(image::ImageView, PixelOrder = PixelOrder::bgr, image::Limits = {});
[[nodiscard]] Result<image::Image> from_mat(const cv::Mat&, PixelOrder, image::Limits = {});
}
