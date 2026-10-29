#pragma once
#include <iiVisualCodec/Geometry.h>
#include <string_view>

namespace ii::visual::asset3d {
using image::Result;
enum class Format { glb, gltf, obj, stl_binary, stl_ascii, ply_binary, ply_ascii, collada, fbx_binary };
struct File final { std::string name; std::vector<std::uint8_t> bytes; };
struct Bundle final {
    std::string main_file;
    std::vector<File> files; // relative virtual paths; owns main file and all sidecars
};
struct ImportOptions final {
    double metres_per_unit = 1; // source coordinates are not guessed or silently rescaled
    geometry::UpAxis up_axis = geometry::UpAxis::y;
    bool generate_normals = true;
};
// Static triangle geometry, node transforms, instances, base-colour/PBR factors,
// encoded base-colour textures. Animated/skinned scenes are explicitly rejected.
// All parser I/O is confined to the supplied virtual bundle; no host filesystem.
[[nodiscard]] Result<geometry::Scene> decode(const Bundle&, ImportOptions = {}, geometry::Limits = {});
[[nodiscard]] Result<geometry::Scene> decode(std::span<const std::uint8_t>, std::string_view extension,
                                            ImportOptions = {}, geometry::Limits = {});
// Export normalizes to metres/+Y up, preserving hierarchy/instances. Formats with
// no material/hierarchy support (STL/PLY) bake instances and omit those attributes.
// main_file names the primary output; every sidecar is returned in the bundle.
[[nodiscard]] Result<Bundle> encode(const geometry::Scene&, Format, std::string_view main_file,
                                   geometry::Limits = {});
[[nodiscard]] std::string backend_version();
}
