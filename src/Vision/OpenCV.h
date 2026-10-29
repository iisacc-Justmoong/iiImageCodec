#pragma once
#include <iiVisualCodec/Vision.h>

namespace ii::visual::opencv {
using image::Result;
enum class Interpolation { nearest, linear, area, cubic };
struct EdgeOptions final { double low=50, high=150; int aperture=3; bool l2_gradient=false; };
struct RegionOptions final { std::size_t minimum_area=1, maximum_area=0; int connectivity=8; };
struct FlowOptions final { unsigned window=21, levels=3; float minimum_eigenvalue=0.0001f; };
[[nodiscard]] std::string backend_version();
[[nodiscard]] Result<image::Image> resize(image::ImageView, std::uint32_t width, std::uint32_t height,
    Interpolation = Interpolation::linear, image::Limits = {});
[[nodiscard]] Result<image::Image> grayscale(image::ImageView, image::Limits = {});
[[nodiscard]] Result<vision::Mask> edges(image::ImageView, EdgeOptions = {}, vision::Limits = {});
// Binary nonzero pixels are foreground. Connected regions are geometric
// components (class 0), not learned semantic categories.
[[nodiscard]] Result<std::vector<vision::Object>> regions(const vision::Mask&, RegionOptions = {}, vision::Limits = {});
[[nodiscard]] Result<vision::FeatureSet> orb_features(image::ImageView, std::size_t maximum=500, vision::Limits = {});
[[nodiscard]] Result<std::vector<vision::TrackedPoint>> track_features(image::ImageView previous,
    image::ImageView current, std::span<const vision::Point>, FlowOptions = {}, vision::Limits = {});
[[nodiscard]] Result<image::Image> draw_objects(image::ImageView, std::span<const vision::Object>,
    std::array<std::uint8_t,3> rgb={0,255,0}, unsigned thickness=2, vision::Limits = {});

class Preprocessor final : public vision::Preprocessor {
public:
    [[nodiscard]] Result<vision::PreparedInput> prepare(image::ImageView, const vision::PreprocessOptions&, vision::Limits = {}) const override;
};
}
