#include <iiVisualCodec/Asset3D.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace ii::visual;
#define CHECK(x) do{if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(false)
template<class T> void require(const T& result){if(!result){std::cerr<<result.error().message<<"\n";std::exit(1);}}
int main(int argc,char** argv){
    geometry::Scene scene;scene.metres_per_unit=.001;scene.up_axis=geometry::UpAxis::z;
    scene.meshes={{"triangle",{{0,0,0},{1000,0,0},{0,1000,0}},{},{},{0,1,2},0}};
    scene.materials={{"red",{.8f,.1f,.2f,1},.2f,.6f}};
    scene.nodes={{"parent",{},{},{}},{"first",0,{}, {0}},{"instance",0,{}, {0}}};scene.nodes[0].transform.matrix[3]=2000;scene.nodes[2].transform.matrix[7]=1000;
    const auto expected=geometry::bounds(scene);CHECK(expected);
    for(auto [format,name]:{std::pair{asset3d::Format::glb,"scene.glb"}, {asset3d::Format::gltf,"scene.gltf"},
        {asset3d::Format::obj,"scene.obj"},{asset3d::Format::stl_binary,"scene.stl"},{asset3d::Format::stl_ascii,"ascii.stl"},
        {asset3d::Format::ply_binary,"scene.ply"},{asset3d::Format::ply_ascii,"ascii.ply"},{asset3d::Format::collada,"scene.dae"},{asset3d::Format::fbx_binary,"scene.fbx"}}) {
        auto encoded=asset3d::encode(scene,format,name);require(encoded);CHECK(!encoded->files.empty());
        auto decoded=asset3d::decode(*encoded);require(decoded);auto actual=geometry::bounds(*decoded);CHECK(actual);
        for(int k=0;k<3;++k){CHECK(std::abs(actual->minimum[k]-expected->minimum[k])<1e-4);CHECK(std::abs(actual->maximum[k]-expected->maximum[k])<1e-4);}
        auto flat=geometry::flatten(*decoded);CHECK(flat);std::size_t triangles=0;for(const auto& m:*flat)triangles+=m.indices.size()/3;CHECK(triangles==2);
        if(format==asset3d::Format::glb||format==asset3d::Format::gltf) {
            CHECK(decoded->meshes.size()==1);CHECK(decoded->materials.size()>=1);bool red=false;
            for(const auto& m:decoded->materials)if(std::abs(m.base_color[0]-.8f)<.01f&&std::abs(m.metallic-.2f)<.01f)red=true;CHECK(red);
        }
        if(format==asset3d::Format::gltf||format==asset3d::Format::obj)CHECK(encoded->files.size()>1);
        if(argc>1){auto root=std::filesystem::path(argv[1])/name;std::filesystem::create_directories(root);for(const auto& f:encoded->files){auto p=root/f.name;std::filesystem::create_directories(p.parent_path());std::ofstream out(p,std::ios::binary);out.write(reinterpret_cast<const char*>(f.bytes.data()),f.bytes.size());CHECK(out.good());}}
    }
    auto nested=asset3d::encode(scene,asset3d::Format::gltf,"nested/scene.gltf");require(nested);require(asset3d::decode(*nested));
    image::Image pixel{1,1,{32,64,128,255}};auto png=image::encode_png(pixel.view());CHECK(png);
    scene.textures={{"pixel.png","png",*png}};scene.materials[0].base_color_texture=0;scene.meshes[0].texcoords={{0,0},{1,0},{0,1}};
    for(auto format:{asset3d::Format::glb,asset3d::Format::gltf}) {
        auto encoded=asset3d::encode(scene,format,format==asset3d::Format::glb?"textured.glb":"textured.gltf");require(encoded);
        auto decoded=asset3d::decode(*encoded);require(decoded);CHECK(decoded->textures.size()==1&&decoded->textures[0].bytes==*png);
        CHECK(decoded->materials[0].base_color_texture.has_value());
    }
    CHECK(!asset3d::encode(scene,asset3d::Format::obj,"out.obj"));
    geometry::Limits tiny;tiny.output_bytes=8;CHECK(!asset3d::encode(scene,asset3d::Format::glb,"tiny.glb",tiny));
    tiny={};tiny.input_bytes=2;const std::vector<std::uint8_t> bytes{'b','a','d'};CHECK(!asset3d::decode(bytes,"glb",{},tiny));
    CHECK(!asset3d::decode(bytes,"glb"));CHECK(!asset3d::decode(bytes,"../glb"));
    CHECK(!asset3d::decode(asset3d::Bundle{"../root.obj",{{"../root.obj",bytes}}}));
    CHECK(!asset3d::decode(asset3d::Bundle{"a.obj",{{"a.obj",bytes},{"./a.obj",bytes}}}));
    CHECK(!asset3d::encode(scene,asset3d::Format::glb,"/outside.glb"));
    // The OBJ parser must resolve material/texture data exclusively from the bundle.
    const std::string obj="mtllib material.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nusemtl painted\nf 1/1 2/2 3/3\n";
    const std::string mtl="newmtl painted\nKd 1 1 1\nd 0.5\nmap_Kd pixel.png\n";
    asset3d::Bundle bundle{"asset.obj",{{"asset.obj",{obj.begin(),obj.end()}},{"material.mtl",{mtl.begin(),mtl.end()}},{"pixel.png",*png}}};
    auto imported=asset3d::decode(bundle);require(imported);CHECK(imported->textures.size()==1);
    bool translucent=false;for(const auto& m:imported->materials)if(m.base_color_texture&&std::abs(m.base_color[3]-.5f)<1e-6)translucent=true;CHECK(translucent);
    bundle.files.pop_back();CHECK(!asset3d::decode(bundle));
    std::cout<<"Assimp "<<asset3d::backend_version()<<": static geometry, instances, 9 format variants, sidecars, PBR, textures and bounds passed\n";
}
