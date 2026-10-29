#pragma once
#include "Geometry.h"
#include "UsdCodec.h"

namespace ii::visual::usd {
// Bake a static scene into a new metre/+Y stage. USD's float mesh representation
// must represent every coordinate. Encoded in-memory textures require caller
// publication to named USD assets first and are explicitly rejected here.
[[nodiscard]] Result<Document> from_geometry(const geometry::Scene&, Limits = {}, geometry::Limits = {});
}
