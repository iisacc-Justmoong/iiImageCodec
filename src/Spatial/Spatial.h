#pragma once
#include <iiVisualCodec/ImageCodec.h>
#include <array>

namespace ii::visual::spatial {
using image::Result;
// Camera space is right handed: +X right, +Y up, camera looks along -Z.
enum class Projection { rectilinear, equirectangular };
enum class StereoLayout { mono, side_by_side, top_bottom };
enum class Eye { left, right };
struct Pose final {
    std::array<double, 3> translation{}; // metres
    std::array<double, 4> rotation{0, 0, 0, 1}; // quaternion x,y,z,w
};
struct Metadata final {
    Projection projection = Projection::rectilinear;
    StereoLayout stereo = StereoLayout::mono;
    Pose pose;
};
struct CameraIntrinsics final {
    std::uint32_t width = 0, height = 0;
    double fx = 0, fy = 0, cx = 0, cy = 0; // pixel centres
};
struct DepthView final {
    std::uint32_t width = 0, height = 0;
    std::size_t row_stride = 0; // floats, zero means width
    std::span<const float> metres;
};
struct PointCloud final { std::vector<std::array<double, 3>> points; };
struct ViewOptions final {
    std::uint32_t width = 1024, height = 1024;
    double horizontal_fov_degrees = 90;
    Pose orientation;
    Eye eye = Eye::left;
};
[[nodiscard]] Result<void> validate(const Metadata&);
[[nodiscard]] Result<image::Image> extract_eye(image::ImageView, StereoLayout, Eye,
                                              image::Limits = {});
[[nodiscard]] Result<image::Image> pack_stereo(image::ImageView left, image::ImageView right,
                                             StereoLayout, image::Limits = {});
// Render an oriented perspective viewport from a full 360x180 equirectangular
// image. Packed stereo is sampled within the selected eye, including the seam.
[[nodiscard]] Result<image::Image> render_view(image::ImageView, const Metadata&,
                                             const ViewOptions&, image::Limits = {});
// Depth is positive distance along camera -Z. Zero, negative and nonfinite
// samples are omitted. Pose transforms camera points into world metres.
[[nodiscard]] Result<PointCloud> unproject_depth(DepthView, const CameraIntrinsics&,
                                               const Pose& = {}, image::Limits = {});
}
