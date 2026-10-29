#include "Geometry.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace ii::visual::geometry {
namespace {
auto fail(image::ErrorCode c, std::string s) { return std::unexpected(image::Error{c,std::move(s)}); }
auto invalid(std::string s) { return fail(image::ErrorCode::invalid_input,std::move(s)); }
auto limit() { return fail(image::ErrorCode::limit_exceeded,"Geometry budget exceeded"); }
template<class T> bool finite(const T& a) { return std::all_of(a.begin(),a.end(),[](auto v){return std::isfinite(v);}); }
double determinant(const Transform& t) {
    const auto& m=t.matrix;
    return m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
}
Vec3 point(const Transform& t, Vec3 p) {
    const auto& m=t.matrix;
    return {m[0]*p[0]+m[1]*p[1]+m[2]*p[2]+m[3],m[4]*p[0]+m[5]*p[1]+m[6]*p[2]+m[7],m[8]*p[0]+m[9]*p[1]+m[10]*p[2]+m[11]};
}
Vec3 cross(Vec3 a, Vec3 b) {return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
Vec3 subtract(Vec3 a,Vec3 b) {return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
void normalize(Vec3& n) { double d=std::hypot(n[0],n[1],n[2]);if(d>0)for(auto& x:n)x/=d; }
}
Result<void> validate(const Transform& t) {
    const auto& m=t.matrix;
    if(!finite(m)||m[12]!=0||m[13]!=0||m[14]!=0||m[15]!=1) return invalid("Expected a finite affine column-vector matrix");
    if(!std::isfinite(determinant(t))||determinant(t)==0) return invalid("Singular geometry transform");
    return {};
}
Result<void> validate(const Mesh& m, Limits l) {
    if(m.positions.empty()||m.indices.empty()||m.indices.size()%3) return invalid("Mesh requires vertices and triangle indices");
    if(m.positions.size()>l.vertices||m.indices.size()>l.indices||m.positions.size()>UINT32_MAX) return limit();
    if((!m.normals.empty()&&m.normals.size()!=m.positions.size())||(!m.texcoords.empty()&&m.texcoords.size()!=m.positions.size())||(!m.source_faces.empty()&&m.source_faces.size()!=m.indices.size()/3)) return invalid("Mesh attribute count mismatch");
    for(const auto& p:m.positions)if(!finite(p))return invalid("Nonfinite vertex");
    for(const auto& p:m.normals)if(!finite(p))return invalid("Nonfinite normal");
    for(const auto& p:m.texcoords)if(!finite(p))return invalid("Nonfinite texture coordinate");
    for(auto i:m.indices)if(i>=m.positions.size())return invalid("Triangle index out of range");
    return {};
}
Result<void> validate(const Scene& s, Limits l) {
    if(!std::isfinite(s.metres_per_unit)||s.metres_per_unit<=0||(s.up_axis!=UpAxis::y&&s.up_axis!=UpAxis::z))return invalid("Invalid scene units or up axis");
    if(s.meshes.empty()||s.nodes.empty())return invalid("Scene requires meshes and nodes");
    if(s.meshes.size()>l.meshes||s.nodes.size()>l.nodes||s.materials.size()>l.materials||s.textures.size()>l.files)return limit();
    std::size_t v=0,i=0,b=0;
    for(const auto& m:s.meshes) {
        if(auto r=validate(m,l);!r)return r;
        if(m.positions.size()>l.vertices-v||m.indices.size()>l.indices-i)return limit();
        v+=m.positions.size();i+=m.indices.size();
        if(m.material&&*m.material>=s.materials.size())return invalid("Material index out of range");
    }
    for(const auto& t:s.textures){if(t.bytes.empty())return invalid("Empty encoded texture");if(t.bytes.size()>l.texture_bytes-b)return limit();b+=t.bytes.size();}
    for(const auto& m:s.materials) {
        if(!finite(m.base_color)||!std::isfinite(m.metallic)||!std::isfinite(m.roughness))return invalid("Nonfinite material");
        for(auto x:m.base_color)if(x<0||x>1)return invalid("Material colour outside [0,1]");
        if(m.metallic<0||m.metallic>1||m.roughness<0||m.roughness>1)return invalid("Material factor outside [0,1]");
        if(m.base_color_texture&&*m.base_color_texture>=s.textures.size())return invalid("Texture index out of range");
    }
    // Parent ordering guarantees acyclic traversal; bound references as well as nodes.
    std::vector<std::size_t> depths;
    try {depths.resize(s.nodes.size());}catch(const std::bad_alloc&){return limit();}
    std::size_t references=0;
    for(std::size_t n=0;n<s.nodes.size();++n) {
        const auto& node=s.nodes[n];
        if(node.parent&&*node.parent>=n)return invalid("Parent must precede child");
        depths[n]=node.parent?depths[*node.parent]+1:1;
        if(depths[n]>l.depth||node.meshes.size()>l.nodes-references)return limit();
        references+=node.meshes.size();
        if(auto r=validate(node.transform);!r)return r;
        for(auto k:node.meshes)if(k>=s.meshes.size())return invalid("Node mesh index out of range");
    }
    return {};
}
Result<Transform> compose(const Transform& a,const Transform& b) {
    if(auto r=validate(a);!r)return std::unexpected(r.error());
    if(auto r=validate(b);!r)return std::unexpected(r.error());
    Transform c;c.matrix.fill(0);
    for(int r=0;r<4;++r)for(int col=0;col<4;++col)for(int k=0;k<4;++k)c.matrix[r*4+col]+=a.matrix[r*4+k]*b.matrix[k*4+col];
    if(auto r=validate(c);!r)return std::unexpected(r.error());
    return c;
}
Result<Vec3> transform_point(const Transform& t,Vec3 p) {
    if(auto r=validate(t);!r)return std::unexpected(r.error());
    if(!finite(p))return invalid("Nonfinite point");
    auto v=point(t,p);if(!finite(v))return invalid("Point transform overflow");return v;
}
Result<Mesh> transformed(const Mesh& src,const Transform& t,Limits l) try {
    if(auto r=validate(src,l);!r)return std::unexpected(r.error());
    if(auto r=validate(t);!r)return std::unexpected(r.error());
    Mesh m=src;const auto& a=t.matrix;const double d=determinant(t);
    const std::array<double,9> cofactor{a[5]*a[10]-a[6]*a[9],a[6]*a[8]-a[4]*a[10],a[4]*a[9]-a[5]*a[8],a[2]*a[9]-a[1]*a[10],a[0]*a[10]-a[2]*a[8],a[1]*a[8]-a[0]*a[9],a[1]*a[6]-a[2]*a[5],a[2]*a[4]-a[0]*a[6],a[0]*a[5]-a[1]*a[4]};
    for(auto& p:m.positions)p=point(t,p);
    for(auto& n:m.normals){Vec3 v=n;for(int r=0;r<3;++r)n[r]=(cofactor[r*3]*v[0]+cofactor[r*3+1]*v[1]+cofactor[r*3+2]*v[2])/d;normalize(n);}
    if(d<0)for(std::size_t i=0;i<m.indices.size();i+=3)std::swap(m.indices[i+1],m.indices[i+2]);
    if(auto r=validate(m,l);!r)return std::unexpected(r.error());return m;
}catch(const std::bad_alloc&){return limit();}
Result<Mesh> with_normals(Mesh m,Limits l) try {
    if(auto r=validate(m,l);!r)return std::unexpected(r.error());
    m.normals.assign(m.positions.size(),{});
    for(std::size_t i=0;i<m.indices.size();i+=3) {
        auto a=m.indices[i],b=m.indices[i+1],c=m.indices[i+2];
        auto n=cross(subtract(m.positions[b],m.positions[a]),subtract(m.positions[c],m.positions[a]));
        if(!finite(n))return invalid("Normal calculation overflow");
        for(auto idx:{a,b,c})for(int k=0;k<3;++k)m.normals[idx][k]+=n[k];
    }
    for(auto& n:m.normals)normalize(n);
    if(auto r=validate(m,l);!r)return std::unexpected(r.error());return m;
}catch(const std::bad_alloc&){return limit();}
Result<std::vector<Mesh>> flatten(const Scene& s,Limits l) try {
    if(auto r=validate(s,l);!r)return std::unexpected(r.error());
    Transform basis;const double u=s.metres_per_unit;
    basis.matrix={u,0,0,0, 0,u,0,0, 0,0,u,0, 0,0,0,1};
    if(s.up_axis==UpAxis::z)basis.matrix={u,0,0,0, 0,0,u,0, 0,-u,0,0, 0,0,0,1};
    std::vector<Transform> world;world.reserve(s.nodes.size());std::vector<Mesh> result;
    std::size_t vertices=0,indices=0;
    for(const auto& n:s.nodes) {
        auto t=compose(n.parent?world[*n.parent]:basis,n.transform);if(!t)return std::unexpected(t.error());world.push_back(*t);
        for(auto k:n.meshes) {
            const auto& m=s.meshes[k];
            if(result.size()>=l.meshes||m.positions.size()>l.vertices-vertices||m.indices.size()>l.indices-indices)return limit();
            auto copy=transformed(m,*t,l);if(!copy)return std::unexpected(copy.error());
            vertices+=m.positions.size();indices+=m.indices.size();result.push_back(std::move(*copy));
        }
    }
    if(result.empty())return invalid("Scene has no mesh instances");return result;
}catch(const std::bad_alloc&){return limit();}
Result<Bounds> bounds(const Scene& s,Limits l) {
    auto meshes=flatten(s,l);if(!meshes)return std::unexpected(meshes.error());
    Bounds b{meshes->front().positions.front(),meshes->front().positions.front()};
    for(const auto& m:*meshes)for(auto p:m.positions)for(int k=0;k<3;++k){b.minimum[k]=std::min(b.minimum[k],p[k]);b.maximum[k]=std::max(b.maximum[k],p[k]);}
    return b;
}
}
