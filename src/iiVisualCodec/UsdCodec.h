#pragma once
#include "Spatial.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

namespace ii::visual::usd {
using image::Result;
enum class Format { usda, usdc, usdz };
enum class UpAxis { y, z };
struct Limits final {
    std::size_t input_bytes = 256 * 1024 * 1024;
    std::size_t output_bytes = 256 * 1024 * 1024;
    std::size_t prims = 100000;
    std::size_t vertices = 10000000;
    std::size_t indices = 30000000;
};
struct StageMetadata final {
    double metres_per_unit = 1;
    UpAxis up_axis = UpAxis::y;
    double time_codes_per_second = 24;
    double start_time = 0, end_time = 0;
};
struct Transform final {
    // Row-major, USD row-vector convention; translation occupies [12..14].
    std::array<double,16> matrix{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
};
struct Mesh final {
    std::string path;
    std::vector<std::array<float,3>> points;
    std::vector<int> face_vertex_counts, face_vertex_indices;
    std::vector<std::array<float,3>> normals; // vertex interpolation
    std::vector<std::array<float,2>> texcoords; // vertex interpolation
    Transform transform;
    std::string material_path;
};
struct Camera final {
    std::string path;
    Transform transform;
    double focal_length = 50, horizontal_aperture = 36, vertical_aperture = 24;
    double near_plane = 0.1, far_plane = 10000;
};
struct Material final {
    std::string path;
    std::array<float,3> diffuse{0.5f,0.5f,0.5f};
    float opacity = 1, roughness = 0.5f, metallic = 0;
    std::string diffuse_texture; // local asset; USDZ export packages dependencies
};
struct Points final {
    std::string path;
    std::vector<std::array<float,3>> points;
    Transform transform;
};
// An owned OpenUSD stage retains root-layer schemas, hierarchy, variants and
// time samples. Mesh/camera/material helpers are conveniences, not a replacement
// scene schema. Independent Documents may be used concurrently; one Document
// requires caller synchronization. Copies share the same stage.
class Document final {
public:
    [[nodiscard]] static Result<Document> create(StageMetadata = {}, Limits = {});
    [[nodiscard]] static Result<Document> from_usda(std::string_view, Limits = {});
    // Native OpenUSD file/resolver access: only use trusted local assets. Input
    // byte limits cover the root file, not its transitive external dependencies.
    [[nodiscard]] static Result<Document> read(const std::filesystem::path&, Limits = {});
    [[nodiscard]] Result<std::string> to_usda() const;
    [[nodiscard]] Result<StageMetadata> metadata() const;
    [[nodiscard]] Result<void> add_mesh(const Mesh&, std::optional<double> time = {});
    [[nodiscard]] Result<void> add_camera(const Camera&, std::optional<double> time = {});
    [[nodiscard]] Result<void> add_material(const Material&);
    [[nodiscard]] Result<void> add_points(const Points&, std::optional<double> time = {});
    [[nodiscard]] Result<std::vector<Mesh>> meshes(std::optional<double> time = {}) const;
    [[nodiscard]] Result<std::vector<Camera>> cameras(std::optional<double> time = {}) const;
    // Preview Surface convenience values. Other shader networks remain in
    // the owned layer and are preserved by root-layer export.
    [[nodiscard]] Result<std::vector<Material>> materials() const;
    [[nodiscard]] Result<std::vector<Points>> point_clouds(std::optional<double> time = {}) const;
    // USDA/USDC export the root layer, preserving composition. USDZ flattens the
    // selected composition and packages referenced assets. Existing files are
    // rejected; the caller owns publication/replace policy.
    [[nodiscard]] Result<void> write(const std::filesystem::path&, Format) const;
private:
    struct Impl;
    explicit Document(std::shared_ptr<Impl> impl):impl_(std::move(impl)){}
    std::shared_ptr<Impl> impl_;
};
[[nodiscard]] std::string backend_version();
}
