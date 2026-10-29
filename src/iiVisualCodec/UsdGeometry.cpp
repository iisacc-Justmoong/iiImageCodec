#include "UsdGeometry.h"
#include <cmath>
#include <limits>

namespace ii::visual::usd {
Result<Document> from_geometry(const geometry::Scene& scene,Limits limits,geometry::Limits geometry_limits) try {
    if(!scene.textures.empty())return std::unexpected(image::Error{image::ErrorCode::unsupported_format,"Publish encoded textures as named USD assets before conversion"});
    auto flat=geometry::flatten(scene,geometry_limits);if(!flat)return std::unexpected(flat.error());
    auto document=Document::create({},limits);if(!document)return document;
    for(std::size_t i=0;i<scene.materials.size();++i) {
        const auto& src=scene.materials[i];Material material;material.path="/Materials/Material_"+std::to_string(i);
        material.diffuse={src.base_color[0],src.base_color[1],src.base_color[2]};material.opacity=src.base_color[3];material.metallic=src.metallic;material.roughness=src.roughness;
        if(auto r=document->add_material(material);!r)return std::unexpected(r.error());
    }
    for(std::size_t i=0;i<flat->size();++i) {
        const auto& src=(*flat)[i];Mesh mesh;mesh.path="/Geometry/Mesh_"+std::to_string(i);
        for(auto p:src.positions){std::array<float,3> out{};for(int k=0;k<3;++k){if(std::abs(p[k])>std::numeric_limits<float>::max())return std::unexpected(image::Error{image::ErrorCode::invalid_input,"Geometry exceeds USD float precision"});out[k]=static_cast<float>(p[k]);}mesh.points.push_back(out);}
        for(auto n:src.normals)mesh.normals.push_back({static_cast<float>(n[0]),static_cast<float>(n[1]),static_cast<float>(n[2])});
        for(auto uv:src.texcoords){if(std::abs(uv[0])>std::numeric_limits<float>::max()||std::abs(uv[1])>std::numeric_limits<float>::max())return std::unexpected(image::Error{image::ErrorCode::invalid_input,"UV exceeds USD float precision"});mesh.texcoords.push_back({static_cast<float>(uv[0]),static_cast<float>(uv[1])});}
        mesh.face_vertex_counts.assign(src.indices.size()/3,3);
        for(auto index:src.indices){if(index>INT32_MAX)return std::unexpected(image::Error{image::ErrorCode::limit_exceeded,"USD index exceeds int32"});mesh.face_vertex_indices.push_back(static_cast<int>(index));}
        if(src.material)mesh.material_path="/Materials/Material_"+std::to_string(*src.material);
        if(auto r=document->add_mesh(mesh);!r)return std::unexpected(r.error());
    }
    return document;
}catch(const std::bad_alloc&){return std::unexpected(image::Error{image::ErrorCode::limit_exceeded,"USD geometry allocation failed"});}
}
