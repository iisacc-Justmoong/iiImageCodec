#include <iiVisualCodec/UsdCodec.h>
#include <iiVisualCodec/UsdGeometry.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
using namespace ii::visual;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while(false)
int main(int argc,char** argv) {
    CHECK(argc==2);const std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
    auto document=usd::Document::create({0.01,usd::UpAxis::z,30,0,30});CHECK(document);
    usd::Material material;material.path="/World/Materials/Red";material.diffuse={1,0,0};CHECK(document->add_material(material));
    usd::Mesh mesh;mesh.path="/World/Triangle";mesh.points={{{0,0,0}},{{1,0,0}},{{0,1,0}}};
    mesh.face_vertex_counts={3};mesh.face_vertex_indices={0,1,2};mesh.material_path=material.path;
    mesh.normals={{{0,0,1}},{{0,0,1}},{{0,0,1}}};mesh.texcoords={{{0,0}},{{1,0}},{{0,1}}};
    mesh.transform.matrix[12]=7;CHECK(document->add_mesh(mesh));
    mesh.points[0][2]=2;CHECK(document->add_mesh(mesh,30));
    usd::Camera camera;camera.path="/World/Camera";camera.transform.matrix[14]=5;CHECK(document->add_camera(camera));
    usd::Points points;points.path="/World/Depth";points.points={{{1,2,3}},{{2,3,4}}};CHECK(document->add_points(points));
    auto text=document->to_usda();CHECK(text && text->starts_with("#usda"));
    CHECK(text->find("UsdPreviewSurface")!=std::string::npos && text->find("timeSamples")!=std::string::npos);
    auto memory=usd::Document::from_usda(*text);CHECK(memory);
    for(auto format:{usd::Format::usda,usd::Format::usdc,usd::Format::usdz}) {
        const auto name=format==usd::Format::usda?"scene.usda":format==usd::Format::usdc?"scene.usdc":"scene.usdz";
        auto path=root/name;if(std::filesystem::exists(path))std::filesystem::remove(path);
        auto written=document->write(path,format);if(!written)std::cerr<<written.error().message<<'\n';CHECK(written);
        CHECK(!document->write(path,format));
        auto read=usd::Document::read(path);if(!read)std::cerr<<read.error().message<<'\n';CHECK(read);
        auto metadata=read->metadata();CHECK(metadata && metadata->metres_per_unit==0.01 && metadata->up_axis==usd::UpAxis::z && metadata->time_codes_per_second==30);
        auto meshes=read->meshes();CHECK(meshes && meshes->size()==1 && (*meshes)[0].points.size()==3 && (*meshes)[0].face_vertex_indices==mesh.face_vertex_indices);
        CHECK((*meshes)[0].transform.matrix[12]==7 && (*meshes)[0].material_path==material.path);
        auto materials=read->materials();CHECK(materials && materials->size()==1 && (*materials)[0].diffuse==material.diffuse && (*materials)[0].roughness==material.roughness);
        auto animated=read->meshes(30);CHECK(animated && (*animated)[0].points[0][2]==2);
        auto cameras=read->cameras();CHECK(cameras && cameras->size()==1 && (*cameras)[0].focal_length==50 && (*cameras)[0].transform.matrix[14]==5);
        auto clouds=read->point_clouds();CHECK(clouds && clouds->size()==1 && (*clouds)[0].points==points.points);
    }
    // Raw layer round trip retains unknown schema data rather than discarding it.
    auto custom=usd::Document::from_usda("#usda 1.0\ndef Xform \"Custom\" {\n custom string app:tag = \"retained\"\n}\n");CHECK(custom);
    auto retained=custom->to_usda();CHECK(retained && retained->find("retained")!=std::string::npos);
    image::Image pixel{1,1,{255,0,0,255}};auto png=image::encode_png(pixel.view());CHECK(png);
    const auto texture_path=std::filesystem::absolute(root/"texture.png");
    {std::ofstream file(texture_path,std::ios::binary);file.write(reinterpret_cast<const char*>(png->data()),png->size());CHECK(file.good());}
    material.diffuse_texture=texture_path.string();CHECK(document->add_material(material));
    auto textured=root/"textured.usdz";if(std::filesystem::exists(textured))std::filesystem::remove(textured);
    auto packaged=document->write(textured,usd::Format::usdz);if(!packaged)std::cerr<<packaged.error().message<<'\n';CHECK(packaged);
    auto packaged_read=usd::Document::read(textured);CHECK(packaged_read);
    auto packaged_text=packaged_read->to_usda();CHECK(packaged_text && packaged_text->find("UsdUVTexture")!=std::string::npos);
    auto packaged_materials=packaged_read->materials();CHECK(packaged_materials && packaged_materials->size()==1 && packaged_materials->front().diffuse_texture.find("texture.png")!=std::string::npos);
    auto unicode=root/std::filesystem::path(u8"공간 장면.usdc");if(std::filesystem::exists(unicode))std::filesystem::remove(unicode);
    CHECK(document->write(unicode,usd::Format::usdc));CHECK(usd::Document::read(unicode));
    mesh.face_vertex_indices[2]=9;CHECK(!document->add_mesh(mesh));mesh.face_vertex_indices[2]=2;
    mesh.path="relative";CHECK(!document->add_mesh(mesh));
    camera.near_plane=100;camera.far_plane=10;CHECK(!document->add_camera(camera));
    CHECK(!usd::Document::from_usda("garbage"));
    usd::Limits tiny;tiny.input_bytes=4;CHECK(!usd::Document::from_usda(*text,tiny));
    tiny={};tiny.vertices=2;auto bounded=usd::Document::create({},tiny);CHECK(bounded);
    mesh.path="/Triangle";CHECK(!bounded->add_mesh(mesh));
    CHECK(!usd::Document::read(root/"missing.usdc"));
    geometry::Scene source;source.metres_per_unit=.001;source.up_axis=geometry::UpAxis::z;
    source.meshes={{"geometry",{{0,0,0},{1000,0,0},{0,1000,0}},{},{},{0,1,2},0}};
    source.materials={{"green",{0,1,0,1},.3f,.7f}};source.nodes={{"root",{},{},{0}}};source.nodes[0].transform.matrix[3]=2000;
    auto converted=usd::from_geometry(source);CHECK(converted);auto converted_meshes=converted->meshes();CHECK(converted_meshes&&converted_meshes->size()==1);
    CHECK((*converted_meshes)[0].points[1][0]==3&&(*converted_meshes)[0].points[2][2]==-1);
    auto converted_materials=converted->materials();CHECK(converted_materials&&converted_materials->size()==1&&converted_materials->front().metallic==.3f);
    source.textures={{"encoded","png",{1}}};CHECK(!usd::from_geometry(source));
    std::cout<<"USD contracts passed: USDA/USDC/USDZ, topology, camera, material, AR points, animation, Geometry bridge and bounds\n";
}
