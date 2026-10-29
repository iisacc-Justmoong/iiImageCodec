#include "Asset3D.h"
#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/version.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>

namespace ii::visual::asset3d {
namespace {
using geometry::Scene;
using geometry::Limits;
struct Failure { image::Error error; };
[[noreturn]] void fail(image::ErrorCode c,std::string s){throw Failure{{c,std::move(s)}};}
[[noreturn]] void invalid(std::string s){fail(image::ErrorCode::invalid_input,std::move(s));}
[[noreturn]] void limit(){fail(image::ErrorCode::limit_exceeded,"3D asset budget exceeded");}
// Virtual paths never reach the operating system. Reject aliases that could
// bypass duplicate-file accounting, URLs, absolute paths and traversal.
std::string path(std::string_view input) {
    std::string name(input);std::replace(name.begin(),name.end(),'\\','/');
    while(name.starts_with("./"))name.erase(0,2);
    if(name.empty()||name.front()=='/'||name.find(':')!=std::string::npos||name.find('\0')!=std::string::npos)return {};
    std::string result;
    for(std::size_t p=0;p<name.size();) {
        auto end=name.find('/',p);if(end==std::string::npos)end=name.size();
        auto part=name.substr(p,end-p);p=end+1;
        if(part=="..")return {};
        if(part.empty()||part==".")continue;
        if(!result.empty())result+='/';result+=part;
    }
    return result;
}
struct Storage {
    std::map<std::string,std::vector<std::uint8_t>> files;
    std::size_t budget=0,total=0,file_limit=0;
    bool writable=false,exceeded=false;
};
class Stream final:public Assimp::IOStream {
public:
    Stream(Storage& s,std::vector<std::uint8_t>& b,bool w):storage(s),bytes(b),writable(w){}
    std::size_t Read(void* out,std::size_t size,std::size_t count) override {
        if(!size)return 0;count=std::min(count,(bytes.size()-cursor)/size);
        if(count)std::memcpy(out,bytes.data()+cursor,size*count);cursor+=size*count;return count;
    }
    std::size_t Write(const void* in,std::size_t size,std::size_t count) override {
        if(!writable||!size)return 0;
        if(count>storage.budget/size||cursor>storage.budget-size*count){storage.exceeded=true;return 0;}
        auto end=cursor+size*count;
        if(end>bytes.size()) {
            auto growth=end-bytes.size();if(growth>storage.budget-storage.total){storage.exceeded=true;return 0;}
            bytes.resize(end);storage.total+=growth;
        }
        std::memcpy(bytes.data()+cursor,in,size*count);cursor=end;return count;
    }
    aiReturn Seek(std::size_t offset,aiOrigin origin) override {
        std::size_t next=0;
        switch(origin) {
        case aiOrigin_SET:next=offset;break;
        case aiOrigin_CUR:if(offset>SIZE_MAX-cursor)return aiReturn_FAILURE;next=cursor+offset;break;
        case aiOrigin_END:if(offset>bytes.size())return aiReturn_FAILURE;next=bytes.size()-offset;break;
        default:return aiReturn_FAILURE;
        }
        if(next>bytes.size()) {
            if(!writable)return aiReturn_FAILURE;
            const auto growth=next-bytes.size();if(growth>storage.budget-storage.total){storage.exceeded=true;return aiReturn_FAILURE;}
            bytes.resize(next);storage.total+=growth;
        }
        cursor=next;return aiReturn_SUCCESS;
    }
    std::size_t Tell() const override{return cursor;}
    std::size_t FileSize() const override{return bytes.size();}
    void Flush() override{}
private:
    Storage& storage;std::vector<std::uint8_t>& bytes;std::size_t cursor=0;bool writable;
};
class Files final:public Assimp::IOSystem {
public:
    explicit Files(Storage& s):storage(s){}
    bool Exists(const char* name) const override{return storage.files.contains(path(name));}
    char getOsSeparator() const override{return '/';}
    Assimp::IOStream* Open(const char* name,const char* mode="rb") override {
        auto key=path(name);if(key.empty())return nullptr;
        bool write=std::strchr(mode,'w')||std::strchr(mode,'a');
        auto found=storage.files.find(key);
        if(write) {
            if(!storage.writable)return nullptr;
            if(found==storage.files.end()) {
                if(storage.files.size()>=storage.file_limit){storage.exceeded=true;return nullptr;}
                found=storage.files.emplace(key,std::vector<std::uint8_t>{}).first;
            }else if(std::strchr(mode,'w')){storage.total-=found->second.size();found->second.clear();}
        }else if(found==storage.files.end())return nullptr;
        return new Stream(storage,found->second,write);
    }
    void Close(Assimp::IOStream* stream) override{delete stream;}
private:Storage& storage;
};
geometry::Transform matrix(const aiMatrix4x4& m) {return {{m.a1,m.a2,m.a3,m.a4,m.b1,m.b2,m.b3,m.b4,m.c1,m.c2,m.c3,m.c4,m.d1,m.d2,m.d3,m.d4}};}
ai_real real(double v) {
    if(!std::isfinite(v)||std::abs(v)>std::numeric_limits<ai_real>::max())invalid("Coordinate exceeds native 3D precision");
    return static_cast<ai_real>(v);
}
aiMatrix4x4 matrix(const geometry::Transform& t) {
    const auto& m=t.matrix;return {real(m[0]),real(m[1]),real(m[2]),real(m[3]),real(m[4]),real(m[5]),real(m[6]),real(m[7]),real(m[8]),real(m[9]),real(m[10]),real(m[11]),real(m[12]),real(m[13]),real(m[14]),real(m[15])};
}
Scene import_scene(const aiScene& native,const Storage& files,const std::string& main,ImportOptions options,Limits limits) {
    if(native.mNumAnimations)fail(image::ErrorCode::unsupported_format,"Animated scenes require an animation-aware document backend");
    if(native.mNumMeshes>limits.meshes||native.mNumMaterials>limits.materials||native.mNumTextures>limits.files)limit();
    Scene scene;scene.metres_per_unit=options.metres_per_unit;scene.up_axis=options.up_axis;
    std::size_t vertices=0,indices=0,texture_bytes=0;
    for(unsigned i=0;i<native.mNumMeshes;++i) {
        const auto& src=*native.mMeshes[i];
        if(src.HasBones()||src.mNumAnimMeshes)fail(image::ErrorCode::unsupported_format,"Skins and morph targets are not static geometry");
        if(src.mNumVertices>limits.vertices-vertices||src.mNumFaces>(limits.indices-indices)/3)limit();
        vertices+=src.mNumVertices;indices+=std::size_t(src.mNumFaces)*3;
        geometry::Mesh mesh;mesh.name=src.mName.C_Str();mesh.material=src.mMaterialIndex;
        mesh.positions.reserve(src.mNumVertices);if(src.HasNormals())mesh.normals.reserve(src.mNumVertices);
        if(src.HasTextureCoords(0))mesh.texcoords.reserve(src.mNumVertices);
        for(unsigned v=0;v<src.mNumVertices;++v) {
            auto p=src.mVertices[v];mesh.positions.push_back({p.x,p.y,p.z});
            if(src.HasNormals()){auto n=src.mNormals[v];mesh.normals.push_back({n.x,n.y,n.z});}
            if(src.HasTextureCoords(0)){auto uv=src.mTextureCoords[0][v];mesh.texcoords.push_back({uv.x,uv.y});}
        }
        for(unsigned f=0;f<src.mNumFaces;++f) {
            const auto& face=src.mFaces[f];if(face.mNumIndices!=3)fail(image::ErrorCode::unsupported_format,"Only triangle surfaces are supported");
            mesh.indices.insert(mesh.indices.end(),face.mIndices,face.mIndices+3);
        }
        scene.meshes.push_back(std::move(mesh));
    }
    std::map<std::string,std::size_t> textures;
    auto texture=[&](std::string name)->std::size_t {
        if(auto found=textures.find(name);found!=textures.end())return found->second;
        if(scene.textures.size()>=limits.files)limit();
        geometry::Texture result;result.name=name;
        if(const auto* embedded=native.GetEmbeddedTexture(name.c_str())) {
            if(!embedded->mHeight) {
                if(embedded->mWidth>limits.texture_bytes-texture_bytes)limit();
                result.format_hint=embedded->achFormatHint;
                const auto* p=reinterpret_cast<const std::uint8_t*>(embedded->pcData);result.bytes.assign(p,p+embedded->mWidth);
            }else {
                if(embedded->mWidth==0||embedded->mHeight>(limits.texture_bytes-texture_bytes)/4/embedded->mWidth)limit();
                image::Image img{embedded->mWidth,embedded->mHeight,std::vector<std::uint8_t>(std::size_t(embedded->mWidth)*embedded->mHeight*4)};
                for(std::size_t i=0;i<img.rgba.size()/4;++i){auto p=embedded->pcData[i];img.rgba[i*4]=p.r;img.rgba[i*4+1]=p.g;img.rgba[i*4+2]=p.b;img.rgba[i*4+3]=p.a;}
                auto png=image::encode_png(img.view());if(!png)throw Failure{png.error()};result.bytes=std::move(*png);result.format_hint="png";
            }
        }else {
            auto key=path(name);auto pos=main.find_last_of('/');
            if(pos!=std::string::npos)key=path(main.substr(0,pos+1)+name);
            auto found=files.files.find(key);if(found==files.files.end())invalid("Missing base-colour texture in virtual bundle: "+name);
            if(found->second.size()>limits.texture_bytes-texture_bytes)limit();result.bytes=found->second;
            auto dot=name.find_last_of('.');if(dot!=std::string::npos)result.format_hint=name.substr(dot+1);
        }
        if(result.bytes.size()>limits.texture_bytes-texture_bytes)limit();texture_bytes+=result.bytes.size();
        auto index=scene.textures.size();scene.textures.push_back(std::move(result));textures.emplace(name,index);return index;
    };
    for(unsigned i=0;i<native.mNumMaterials;++i) {
        const auto& src=*native.mMaterials[i];geometry::Material material;aiString name;src.Get(AI_MATKEY_NAME,name);material.name=name.C_Str();
        aiColor4D color(.5f,.5f,.5f,1);
        if(aiGetMaterialColor(&src,AI_MATKEY_BASE_COLOR,&color)!=aiReturn_SUCCESS){aiGetMaterialColor(&src,AI_MATKEY_COLOR_DIFFUSE,&color);src.Get(AI_MATKEY_OPACITY,color.a);}
        material.base_color={color.r,color.g,color.b,color.a};
        src.Get(AI_MATKEY_METALLIC_FACTOR,material.metallic);src.Get(AI_MATKEY_ROUGHNESS_FACTOR,material.roughness);
        aiString tex;if(src.GetTexture(aiTextureType_BASE_COLOR,0,&tex)==aiReturn_SUCCESS||src.GetTexture(aiTextureType_DIFFUSE,0,&tex)==aiReturn_SUCCESS)material.base_color_texture=texture(tex.C_Str());
        scene.materials.push_back(std::move(material));
    }
    if(!native.mRootNode)invalid("3D asset has no root node");
    std::function<void(const aiNode*,std::optional<std::size_t>,std::size_t)> visit;
    visit=[&](const aiNode* node,std::optional<std::size_t> parent,std::size_t depth) {
        if(depth>limits.depth||scene.nodes.size()>=limits.nodes)limit();
        const auto index=scene.nodes.size();geometry::Node result{node->mName.C_Str(),parent,matrix(node->mTransformation),{}};
        if(node->mNumMeshes>limits.nodes)limit();
        for(unsigned i=0;i<node->mNumMeshes;++i)result.meshes.push_back(node->mMeshes[i]);scene.nodes.push_back(std::move(result));
        for(unsigned i=0;i<node->mNumChildren;++i)visit(node->mChildren[i],index,depth+1);
    };
    visit(native.mRootNode,{},1);
    if(auto valid=geometry::validate(scene,limits);!valid)throw Failure{valid.error()};return scene;
}
Scene normalized(const Scene& source,Limits limits) {
    Scene scene=source;geometry::Transform basis,inverse;const double u=source.metres_per_unit;
    basis.matrix={u,0,0,0,0,u,0,0,0,0,u,0,0,0,0,1};inverse.matrix={1/u,0,0,0,0,1/u,0,0,0,0,1/u,0,0,0,0,1};
    if(source.up_axis==geometry::UpAxis::z){basis.matrix={u,0,0,0,0,0,u,0,0,-u,0,0,0,0,0,1};inverse.matrix={1/u,0,0,0,0,0,-1/u,0,0,1/u,0,0,0,0,0,1};}
    for(auto& mesh:scene.meshes){auto m=geometry::transformed(mesh,basis,limits);if(!m)throw Failure{m.error()};mesh=std::move(*m);}
    for(auto& node:scene.nodes){auto t=geometry::compose(basis,node.transform);if(!t)throw Failure{t.error()};t=geometry::compose(*t,inverse);if(!t)throw Failure{t.error()};node.transform=*t;}
    scene.metres_per_unit=1;scene.up_axis=geometry::UpAxis::y;return scene;
}
std::unique_ptr<aiScene> export_scene(const Scene& source) {
    auto out=std::make_unique<aiScene>();out->mNumMeshes=static_cast<unsigned>(source.meshes.size());out->mMeshes=new aiMesh*[out->mNumMeshes]{};
    for(unsigned i=0;i<out->mNumMeshes;++i) {
        const auto& src=source.meshes[i];auto* mesh=out->mMeshes[i]=new aiMesh;
        mesh->mName=aiString(src.name);mesh->mPrimitiveTypes=aiPrimitiveType_TRIANGLE;mesh->mMaterialIndex=static_cast<unsigned>(src.material.value_or(source.materials.size()));
        mesh->mNumVertices=static_cast<unsigned>(src.positions.size());mesh->mVertices=new aiVector3D[mesh->mNumVertices];
        if(!src.normals.empty())mesh->mNormals=new aiVector3D[mesh->mNumVertices];
        if(!src.texcoords.empty()){mesh->mTextureCoords[0]=new aiVector3D[mesh->mNumVertices];mesh->mNumUVComponents[0]=2;}
        for(unsigned v=0;v<mesh->mNumVertices;++v) {
            auto p=src.positions[v];mesh->mVertices[v]={real(p[0]),real(p[1]),real(p[2])};
            if(mesh->mNormals){auto n=src.normals[v];mesh->mNormals[v]={real(n[0]),real(n[1]),real(n[2])};}
            if(mesh->mTextureCoords[0]){auto uv=src.texcoords[v];mesh->mTextureCoords[0][v]={real(uv[0]),real(uv[1]),0};}
        }
        mesh->mNumFaces=static_cast<unsigned>(src.indices.size()/3);mesh->mFaces=new aiFace[mesh->mNumFaces];
        for(unsigned f=0;f<mesh->mNumFaces;++f){auto& face=mesh->mFaces[f];face.mNumIndices=3;face.mIndices=new unsigned[3]{src.indices[f*3],src.indices[f*3+1],src.indices[f*3+2]};}
    }
    out->mNumMaterials=static_cast<unsigned>(source.materials.size()+1);out->mMaterials=new aiMaterial*[out->mNumMaterials]{};
    for(unsigned i=0;i<out->mNumMaterials;++i) {
        auto* m=out->mMaterials[i]=new aiMaterial;geometry::Material src;if(i<source.materials.size())src=source.materials[i];
        aiString name(src.name);m->AddProperty(&name,AI_MATKEY_NAME);auto c=src.base_color;aiColor4D color(c[0],c[1],c[2],c[3]);
        m->AddProperty(&color,1,AI_MATKEY_BASE_COLOR);m->AddProperty(&color,1,AI_MATKEY_COLOR_DIFFUSE);
        m->AddProperty(&src.metallic,1,AI_MATKEY_METALLIC_FACTOR);m->AddProperty(&src.roughness,1,AI_MATKEY_ROUGHNESS_FACTOR);
        m->AddProperty(&c[3],1,AI_MATKEY_OPACITY);
        if(src.base_color_texture){aiString name("*"+std::to_string(*src.base_color_texture));m->AddProperty(&name,AI_MATKEY_TEXTURE(aiTextureType_BASE_COLOR,0));m->AddProperty(&name,AI_MATKEY_TEXTURE(aiTextureType_DIFFUSE,0));}
    }
    out->mNumTextures=static_cast<unsigned>(source.textures.size());if(out->mNumTextures)out->mTextures=new aiTexture*[out->mNumTextures]{};
    for(unsigned i=0;i<out->mNumTextures;++i) {
        const auto& src=source.textures[i];if(src.bytes.size()>UINT32_MAX)limit();auto* tex=out->mTextures[i]=new aiTexture;
        tex->mFilename=aiString(src.name);tex->mWidth=static_cast<unsigned>(src.bytes.size());tex->mHeight=0;
        std::strncpy(tex->achFormatHint,src.format_hint.c_str(),sizeof(tex->achFormatHint)-1);tex->pcData=new aiTexel[(src.bytes.size()+sizeof(aiTexel)-1)/sizeof(aiTexel)]{};
        std::memcpy(tex->pcData,src.bytes.data(),src.bytes.size());
    }
    std::vector<std::vector<std::size_t>> children(source.nodes.size());std::vector<std::size_t> roots;
    for(std::size_t i=0;i<source.nodes.size();++i){if(auto p=source.nodes[i].parent)children[*p].push_back(i);else roots.push_back(i);}
    std::function<std::unique_ptr<aiNode>(std::size_t,aiNode*)> node;
    node=[&](std::size_t index,aiNode* parent){
        const auto& src=source.nodes[index];auto n=std::make_unique<aiNode>(src.name);n->mParent=parent;n->mTransformation=matrix(src.transform);
        n->mNumMeshes=static_cast<unsigned>(src.meshes.size());if(n->mNumMeshes)n->mMeshes=new unsigned[n->mNumMeshes];
        for(unsigned i=0;i<n->mNumMeshes;++i)n->mMeshes[i]=static_cast<unsigned>(src.meshes[i]);
        n->mNumChildren=static_cast<unsigned>(children[index].size());if(n->mNumChildren)n->mChildren=new aiNode*[n->mNumChildren]{};
        for(unsigned i=0;i<n->mNumChildren;++i)n->mChildren[i]=node(children[index][i],n.get()).release();return n;
    };
    out->mRootNode=new aiNode("iiVisualCodec");out->mRootNode->mNumChildren=static_cast<unsigned>(roots.size());out->mRootNode->mChildren=new aiNode*[roots.size()]{};
    for(unsigned i=0;i<roots.size();++i)out->mRootNode->mChildren[i]=node(roots[i],out->mRootNode).release();return out;
}
const char* exporter(Format f) {
    switch(f){case Format::glb:return "glb2";case Format::gltf:return "gltf2";case Format::obj:return "obj";
    case Format::stl_binary:return "stlb";case Format::stl_ascii:return "stl";case Format::ply_binary:return "plyb";
    case Format::ply_ascii:return "ply";case Format::collada:return "collada";case Format::fbx_binary:return "fbx";}
    fail(image::ErrorCode::unsupported_format,"Unknown 3D output format");
}
}
Result<Scene> decode(const Bundle& bundle,ImportOptions options,Limits limits) try {
    Storage files;files.budget=limits.input_bytes;files.file_limit=limits.files;
    if(bundle.files.empty()||bundle.files.size()>limits.files)limit();auto main=path(bundle.main_file);if(main.empty())invalid("Invalid virtual root filename");
    for(const auto& file:bundle.files){auto name=path(file.name);if(name.empty()||files.files.contains(name))invalid("Unsafe or duplicate virtual filename");if(file.bytes.size()>limits.input_bytes-files.total)limit();files.total+=file.bytes.size();files.files.emplace(name,file.bytes);}
    if(!files.files.contains(main))invalid("Missing root asset in bundle");
    Assimp::Importer importer;importer.SetIOHandler(new Files(files));
    unsigned flags=aiProcess_Triangulate|aiProcess_JoinIdenticalVertices|aiProcess_ValidateDataStructure;
    if(options.generate_normals)flags|=aiProcess_GenSmoothNormals;
    const auto* scene=importer.ReadFile(main,flags);
    if(!scene)fail(image::ErrorCode::codec_failure,importer.GetErrorString());
    return import_scene(*scene,files,main,options,limits);
}catch(const Failure& e){return std::unexpected(e.error);}catch(const std::bad_alloc&){return std::unexpected(image::Error{image::ErrorCode::limit_exceeded,"3D allocation failed"});}catch(const std::exception& e){return std::unexpected(image::Error{image::ErrorCode::codec_failure,e.what()});}
Result<Scene> decode(std::span<const std::uint8_t> bytes,std::string_view extension,ImportOptions options,Limits limits) try {
    if(bytes.empty())invalid("Empty 3D asset");if(bytes.size()>limits.input_bytes)limit();
    std::string ext(extension);if(ext.starts_with('.'))ext.erase(0,1);
    if(ext.empty()||ext.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")!=std::string::npos)invalid("Expected a 3D filename extension");
    Bundle bundle;bundle.main_file="asset."+ext;bundle.files.push_back({bundle.main_file,{bytes.begin(),bytes.end()}});return decode(bundle,options,limits);
}catch(const Failure& e){return std::unexpected(e.error);}catch(const std::bad_alloc&){return std::unexpected(image::Error{image::ErrorCode::limit_exceeded,"3D allocation failed"});}
Result<Bundle> encode(const Scene& source,Format format,std::string_view main_file,Limits limits) try {
    if(auto r=geometry::validate(source,limits);!r)throw Failure{r.error()};auto main=path(main_file);if(main.empty())invalid("Invalid 3D output filename");
    auto scene=normalized(source,limits);
    bool flat=format==Format::stl_binary||format==Format::stl_ascii||format==Format::ply_ascii||format==Format::ply_binary;
    if(flat) {
        auto meshes=geometry::flatten(scene,limits);if(!meshes)throw Failure{meshes.error()};scene.meshes=std::move(*meshes);scene.nodes={{"mesh",{},{},{}}};
        for(std::size_t i=0;i<scene.meshes.size();++i)scene.nodes[0].meshes.push_back(i);
    }
    // Embedded base-colour textures are retained in glTF/GLB. Other exporters
    // have incompatible texture publication policies, so require explicit removal.
    if(!scene.textures.empty()&&format!=Format::gltf&&format!=Format::glb)fail(image::ErrorCode::unsupported_format,"Textured export currently requires glTF or GLB");
    Storage output;output.writable=true;output.budget=limits.output_bytes;output.file_limit=limits.files;
    auto native=export_scene(scene);Assimp::Exporter writer;writer.SetIOHandler(new Files(output));
    auto status=writer.Export(native.get(),exporter(format),main);
    if(output.exceeded)limit();if(status!=aiReturn_SUCCESS)fail(image::ErrorCode::codec_failure,writer.GetErrorString());
    if(!output.files.contains(main)||output.files[main].empty())fail(image::ErrorCode::codec_failure,"3D exporter produced no primary file");
    Bundle result;result.main_file=main;for(auto& [name,bytes]:output.files)result.files.push_back({name,std::move(bytes)});return result;
}catch(const Failure& e){return std::unexpected(e.error);}catch(const std::bad_alloc&){return std::unexpected(image::Error{image::ErrorCode::limit_exceeded,"3D allocation failed"});}catch(const std::exception& e){return std::unexpected(image::Error{image::ErrorCode::codec_failure,e.what()});}
std::string backend_version(){return std::to_string(aiGetVersionMajor())+"."+std::to_string(aiGetVersionMinor())+"."+std::to_string(aiGetVersionPatch());}
}
