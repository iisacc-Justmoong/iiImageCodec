#include <iiVisualCodec/Geometry.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace ii::visual;
#define CHECK(x) do { if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);} }while(false)
int main(){
    geometry::Mesh triangle{"triangle",{{0,0,0},{1,0,0},{0,1,0}},{},{},{0,1,2}};
    auto normals=geometry::with_normals(triangle);CHECK(normals&&normals->normals[0][2]==1);
    geometry::Scene scene;scene.metres_per_unit=.001;scene.up_axis=geometry::UpAxis::z;scene.meshes={*normals};
    scene.nodes={{"root",{}, {},{}},{"child",0,{}, {0}}};scene.nodes[0].transform.matrix[3]=1000;
    auto box=geometry::bounds(scene);CHECK(box&&std::abs(box->minimum[0]-1)<1e-12&&std::abs(box->minimum[2]+.001)<1e-12);
    geometry::Transform reflection;reflection.matrix[0]=-2;
    auto reflected=geometry::transformed(*normals,reflection);CHECK(reflected&&reflected->indices[1]==2&&reflected->normals[0][2]==1);
    geometry::Transform shear;shear.matrix[2]=2;
    auto transformed=geometry::transformed(*normals,shear);CHECK(transformed&&transformed->normals[0][2]==1);
    geometry::Mesh tilted{"tilted",{{0,0,0},{1,0,-1},{0,1,-1}},{},{},{0,1,2}};
    auto tilted_normals=geometry::with_normals(tilted);CHECK(tilted_normals);
    geometry::Transform stretch;stretch.matrix[0]=2;auto stretched=geometry::transformed(*tilted_normals,stretch);CHECK(stretched);
    const auto n=stretched->normals[0];CHECK(std::abs(n[1]-n[2])<1e-12&&std::abs(n[0]*2-n[1])<1e-12);
    auto bad=triangle;bad.indices[1]=3;CHECK(!geometry::validate(bad));
    bad=triangle;bad.positions[0][0]=std::numeric_limits<double>::infinity();CHECK(!geometry::validate(bad));
    scene.nodes[1].parent=1;CHECK(!geometry::validate(scene));scene.nodes[1].parent=0;
    geometry::Limits limit;limit.vertices=2;CHECK(!geometry::flatten(scene,limit));
    limit={};limit.depth=1;CHECK(!geometry::validate(scene,limit));
    limit={};limit.vertices=3;scene.nodes.push_back({"instance",0,{}, {0}});CHECK(!geometry::flatten(scene,limit));
    reflection.matrix[0]=0;CHECK(!geometry::validate(reflection));
    std::cout<<"Geometry contracts passed: topology, transforms, units, winding, normals and limits\n";
}
