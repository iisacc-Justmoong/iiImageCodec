#pragma once
#include "Geometry.h"
#include <filesystem>
#include <memory>

namespace ii::visual::cad {
using image::Result;
enum class Format { step, iges, brep };
enum class BooleanOperation { unite, subtract, intersect };
struct Limits final {
    geometry::Limits geometry;
    std::size_t topology = 1000000;
};
struct Properties final {
    bool valid = false;
    std::size_t vertices = 0, edges = 0, faces = 0, shells = 0, solids = 0;
    double surface_area = 0, volume = 0; // mm^2, mm^3; volume only when solids exist
    geometry::Vec3 centre_of_mass{}; // mm
    geometry::Bounds bounds; // mm, native CAD axes
};
struct TessellationOptions final {
    double linear_deflection = 0.1; // absolute mm
    double angular_deflection = 0.5; // radians, (0, pi)
};
// Immutable, shared owned BRep; operations return independent shapes. All native
// CAD coordinates are millimetres. A scene made from tessellate() uses .001 m/unit.
// STEP/IGES exchange is serialized within this module (OCCT IGES global state).
class Shape final {
public:
    [[nodiscard]] static Result<Shape> box(geometry::Vec3 dimensions, Limits = {});
    [[nodiscard]] static Result<Shape> cylinder(double radius, double height, Limits = {});
    [[nodiscard]] static Result<Shape> sphere(double radius, Limits = {});
    [[nodiscard]] static Result<Shape> extrude(std::span<const geometry::Vec2> polygon, double height, Limits = {});
    // Memory input supports STEP/BREP; IGES uses the explicit file method below.
    [[nodiscard]] static Result<Shape> decode(std::span<const std::uint8_t>, Format, Limits = {});
    // OCCT's IGES reader requires native local file I/O. STEP/BREP also accept
    // this convenience path. Trusted local input; no implicit temporary files.
    [[nodiscard]] static Result<Shape> read(const std::filesystem::path&, Format, Limits = {});
    [[nodiscard]] Result<std::vector<std::uint8_t>> encode(Format) const;
    [[nodiscard]] Result<Shape> boolean_with(const Shape&, BooleanOperation) const;
    [[nodiscard]] Result<Shape> transformed(const geometry::Transform&) const;
    [[nodiscard]] Result<Properties> properties() const;
    [[nodiscard]] Result<geometry::Mesh> tessellate(TessellationOptions = {}) const;
private:
    struct Impl;
    explicit Shape(std::shared_ptr<const Impl> p):impl_(std::move(p)){}
    std::shared_ptr<const Impl> impl_;
};
[[nodiscard]] std::string backend_version();
}
