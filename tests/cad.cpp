#include <iiVisualCodec/CadCodec.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <numbers>
using namespace ii::visual;
#define CHECK(x) do{if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(false)
template<class T> void require(const T& r){if(!r){std::cerr<<r.error().message<<"\n";std::exit(1);}}
bool near(double a,double b){return std::abs(a-b)<1e-6*std::max(1.0,std::abs(b));}
double volume(const cad::Shape& shape){auto p=shape.properties();require(p);return p->volume;}
double signed_volume(const geometry::Mesh& mesh){
    double result=0;
    for(std::size_t i=0;i<mesh.indices.size();i+=3){auto a=mesh.positions[mesh.indices[i]],b=mesh.positions[mesh.indices[i+1]],c=mesh.positions[mesh.indices[i+2]];result+=(a[0]*(b[1]*c[2]-b[2]*c[1])+a[1]*(b[2]*c[0]-b[0]*c[2])+a[2]*(b[0]*c[1]-b[1]*c[0]))/6;}
    return result;
}
int main(int argc,char** argv){
    const auto directory=argc>1?std::filesystem::path(argv[1]):std::filesystem::current_path()/"cad-contracts";
    std::filesystem::create_directories(directory);
    auto box=cad::Shape::box({10,20,30});require(box);auto props=box->properties();require(props);
    CHECK(props->valid&&props->vertices==8&&props->edges==12&&props->faces==6&&props->solids==1);
    CHECK(near(props->volume,6000)&&near(props->surface_area,2200));CHECK(near(props->centre_of_mass[0],5));
    auto sphere=cad::Shape::sphere(2);require(sphere);CHECK(near(volume(*sphere),4.0/3*std::numbers::pi*8));
    auto cylinder=cad::Shape::cylinder(2,5);require(cylinder);CHECK(near(volume(*cylinder),20*std::numbers::pi));
    const std::vector<geometry::Vec2> polygon{{0,0},{4,0},{4,5},{0,5}};
    auto prism=cad::Shape::extrude(polygon,3);require(prism);CHECK(near(volume(*prism),60));
    auto clockwise=polygon;std::reverse(clockwise.begin(),clockwise.end());auto reversed=cad::Shape::extrude(clockwise,3);require(reversed);CHECK(near(volume(*reversed),60));
    const std::vector<geometry::Vec2> crossing{{0,0},{4,5},{0,5},{4,0}};CHECK(!cad::Shape::extrude(crossing,3));
    auto a=cad::Shape::box({10,10,10});require(a);geometry::Transform move;move.matrix[3]=5;auto b=a->transformed(move);require(b);
    auto moved_sphere=sphere->transformed(move);require(moved_sphere);CHECK(std::abs(volume(*moved_sphere)-volume(*sphere))<1e-10);
    geometry::Transform uniform;uniform.matrix[0]=uniform.matrix[5]=uniform.matrix[10]=2;
    auto large_sphere=sphere->transformed(uniform);require(large_sphere);CHECK(near(volume(*large_sphere),8*volume(*sphere)));
    for(auto [op,v]:{std::pair{cad::BooleanOperation::unite,1500.0},{cad::BooleanOperation::subtract,500.0},{cad::BooleanOperation::intersect,500.0}}){auto result=a->boolean_with(*b,op);require(result);CHECK(near(volume(*result),v));}
    CHECK(near(volume(*a),1000)); // Boolean operands remain immutable.
    geometry::Transform scale;scale.matrix[0]=2;auto scaled=box->transformed(scale);require(scaled);CHECK(near(volume(*scaled),12000));
    for(auto [format,name]:{std::pair{cad::Format::step,"box.step"},{cad::Format::iges,"box.iges"},{cad::Format::brep,"box.brep"}}) {
        auto bytes=box->encode(format);require(bytes);CHECK(bytes->size()>100);
        const auto path=directory/name;
        {std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes->data()),bytes->size());CHECK(out.good());}
        auto decoded=format==cad::Format::iges?cad::Shape::read(path,format):cad::Shape::decode(*bytes,format);require(decoded);auto p=decoded->properties();require(p);
        CHECK(p->valid&&p->faces==6&&near(p->surface_area,2200));CHECK(near(p->bounds.maximum[0]-p->bounds.minimum[0],10));
        if(p->solids)CHECK(near(p->volume,6000));if(format!=cad::Format::iges)CHECK(p->solids==1);
        CHECK(cad::Shape::read(path,format));
    }
    auto mesh=box->tessellate();require(mesh);CHECK(mesh->indices.size()==36&&mesh->source_faces.size()==12&&mesh->normals.size()==mesh->positions.size());
    CHECK(near(signed_volume(*mesh),6000));CHECK(near(volume(*box),6000));
    geometry::Transform reflection;reflection.matrix[0]=-1;auto mirrored=box->transformed(reflection);require(mirrored);
    auto mirrored_mesh=mirrored->tessellate();require(mirrored_mesh);CHECK(near(signed_volume(*mirrored_mesh),6000));
    CHECK(!box->tessellate({0,.5}));CHECK(!cad::Shape::box({0,1,1}));CHECK(!cad::Shape::sphere(-1));
    cad::Limits tiny;tiny.topology=2;CHECK(!cad::Shape::box({1,1,1},tiny));
    tiny={};tiny.geometry.output_bytes=8;auto small=cad::Shape::box({1,1,1},tiny);require(small);CHECK(!small->encode(cad::Format::brep));CHECK(!small->encode(cad::Format::step));
    tiny={};tiny.geometry.vertices=2;small=cad::Shape::box({1,1,1},tiny);require(small);CHECK(!small->tessellate());
    const std::vector<std::uint8_t> bad{'b','a','d'};CHECK(!cad::Shape::decode(bad,cad::Format::step));CHECK(!cad::Shape::decode(bad,cad::Format::brep));
    CHECK(!cad::Shape::decode(bad,cad::Format::iges));CHECK(!cad::Shape::read(directory/"missing.step",cad::Format::step));
    auto first=std::async(std::launch::async,[&]{auto encoded=box->encode(cad::Format::step);if(!encoded)return false;auto decoded=cad::Shape::decode(*encoded,cad::Format::step);return decoded&&near(volume(*decoded),6000);});
    auto second=std::async(std::launch::async,[&]{auto decoded=cad::Shape::read(directory/"box.iges",cad::Format::iges);if(!decoded)return false;auto p=decoded->properties();return p&&near(p->surface_area,2200);});CHECK(first.get()&&second.get());
    std::cout<<"Open CASCADE "<<cad::backend_version()<<": exact primitives, extrusion, Booleans, metrics, STEP/IGES/BREP and tessellation passed\n";
}
