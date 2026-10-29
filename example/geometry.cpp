#include <iiVisualCodec/Asset3D.h>
#include <iiVisualCodec/CadCodec.h>
#include <iiVisualCodec/UsdGeometry.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace ii::visual;
template<class T> T value(image::Result<T> result){if(!result)throw std::runtime_error(result.error().message);return std::move(*result);}
void write(const std::filesystem::path& path,std::span<const std::uint8_t> bytes){
    std::filesystem::create_directories(path.parent_path());std::ofstream stream(path,std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(!stream)throw std::runtime_error("Cannot publish example output");
}
int main(int argc,char** argv) try {
    if(argc>2)throw std::runtime_error("Usage: geometry_pipeline [new-output-directory]");
    auto block=value(cad::Shape::box({20,20,5}));auto tool=value(cad::Shape::cylinder(4,7));
    geometry::Transform placement;placement.matrix[3]=10;placement.matrix[7]=10;placement.matrix[11]=-1;
    tool=value(tool.transformed(placement));auto part=value(block.boolean_with(tool,cad::BooleanOperation::subtract));
    const auto properties=value(part.properties());
    if(std::abs(properties.volume-(2000-80*std::numbers::pi))>1e-6)throw std::runtime_error("Unexpected CAD volume: "+std::to_string(properties.volume));
    geometry::Scene scene;scene.metres_per_unit=.001;scene.up_axis=geometry::UpAxis::z;
    scene.meshes={value(part.tessellate())};scene.meshes[0].material=0;scene.materials={{"blue",{.12f,.35f,.8f,1},.1f,.4f}};
    scene.nodes={{"Part",{},{},{0}}};
    auto glb=value(asset3d::encode(scene,asset3d::Format::glb,"part.glb"));
    auto decoded=value(asset3d::decode(glb));auto box=value(geometry::bounds(decoded));
    if(std::abs(box.maximum[0]-.020)>1e-6||std::abs(box.maximum[1]-.005)>1e-6)throw std::runtime_error("Unit/up-axis conversion failed");
    auto usd_document=value(usd::from_geometry(scene));auto usda=value(usd_document.to_usda());
    auto reopened=value(usd::Document::from_usda(usda));if(value(reopened.meshes()).empty())throw std::runtime_error("USD conversion lost mesh");
    if(argc==2) {
        const std::filesystem::path directory(argv[1]);if(std::filesystem::exists(directory))throw std::runtime_error("Output directory already exists");
        std::filesystem::create_directories(directory);
        for(const auto& file:glb.files)write(directory/file.name,file.bytes);
        auto stl=value(asset3d::encode(scene,asset3d::Format::stl_binary,"part.stl"));for(const auto& file:stl.files)write(directory/file.name,file.bytes);
        write(directory/"part.step",value(part.encode(cad::Format::step)));
        write(directory/"part.usda",{reinterpret_cast<const std::uint8_t*>(usda.data()),usda.size()});
    }
    std::cout<<"CAD volume="<<properties.volume<<" mm^3, triangles="<<scene.meshes[0].indices.size()/3
             <<", GLB files="<<glb.files.size()<<", USD bytes="<<usda.size()<<"\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
