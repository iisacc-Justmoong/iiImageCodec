#pragma once
#include "ImageCodec.h"
#include <array>
#include <optional>

namespace ii::visual::geometry {
using image::Result;
using Vec2 = std::array<double,2>;
using Vec3 = std::array<double,3>;
enum class UpAxis { y, z };
struct Limits final {
    std::size_t input_bytes = 256 * 1024 * 1024;
    std::size_t output_bytes = 256 * 1024 * 1024;
    std::size_t vertices = 10000000, indices = 30000000;
    std::size_t meshes = 100000, nodes = 100000, materials = 100000;
    std::size_t texture_bytes = 256 * 1024 * 1024;
    std::size_t files = 1024, depth = 256;
};
struct Transform final {
    // Row-major, column vectors; translation is [3,7,11]. Affine only.
    std::array<double,16> matrix{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
};
struct Bounds final { Vec3 minimum{}, maximum{}; };
struct Mesh final {
    std::string name;
    std::vector<Vec3> positions, normals;
    std::vector<Vec2> texcoords;
    std::vector<std::uint32_t> indices; // CCW triangles
    std::optional<std::size_t> material;
    // Optional CAD face identifier for each triangle, stable within one tessellation.
    std::vector<std::uint32_t> source_faces;
};
struct Texture final {
    std::string name, format_hint;
    std::vector<std::uint8_t> bytes; // encoded image, owned
};
struct Material final {
    std::string name;
    std::array<float,4> base_color{0.5f,0.5f,0.5f,1};
    float metallic = 0, roughness = 0.5f;
    std::optional<std::size_t> base_color_texture;
};
struct Node final {
    std::string name;
    std::optional<std::size_t> parent; // parent must precede child; roots have no parent
    Transform transform;
    std::vector<std::size_t> meshes; // references allow instancing
};
struct Scene final {
    double metres_per_unit = 1;
    UpAxis up_axis = UpAxis::y;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<Node> nodes;
};
[[nodiscard]] Result<void> validate(const Transform&);
[[nodiscard]] Result<void> validate(const Mesh&, Limits = {});
[[nodiscard]] Result<void> validate(const Scene&, Limits = {});
[[nodiscard]] Result<Transform> compose(const Transform& parent, const Transform& local);
[[nodiscard]] Result<Vec3> transform_point(const Transform&, Vec3);
[[nodiscard]] Result<Mesh> transformed(const Mesh&, const Transform&, Limits = {});
[[nodiscard]] Result<Mesh> with_normals(Mesh, Limits = {});
// Bake hierarchy/instances, convert to metres and +Y up. Reverses winding for
// reflected transforms and uses inverse transpose for normals.
[[nodiscard]] Result<std::vector<Mesh>> flatten(const Scene&, Limits = {});
[[nodiscard]] Result<Bounds> bounds(const Scene&, Limits = {}); // metres, +Y up
}
