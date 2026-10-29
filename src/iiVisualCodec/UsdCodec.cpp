#include "UsdCodec.h"
#include "Visual_p.h"
#include <pxr/pxr.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdUtils/usdzPackage.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>
#include <type_traits>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE
namespace ii::visual::usd {
namespace {
auto invalid(std::string message){return detail::error(image::ErrorCode::invalid_input,std::move(message));}
auto limit_error(std::string message){return detail::error(image::ErrorCode::limit_exceeded,std::move(message));}
std::string utf8_path(const std::filesystem::path& path){
    const auto text=path.u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};
}
auto failed(std::string message,TfErrorMark& mark){
    for(auto it=mark.GetBegin();it!=mark.GetEnd();++it){message+=" : ";message+=it->GetCommentary();}
    mark.Clear();return detail::error(image::ErrorCode::codec_failure,std::move(message));
}
bool valid_time(std::optional<double> time){return !time || std::isfinite(*time);}
UsdTimeCode time_code(std::optional<double> time){return time?UsdTimeCode(*time):UsdTimeCode::Default();}
bool valid_path(const std::string& path){
    if(!SdfPath::IsValidPathString(path))return false;
    const SdfPath p(path);return p.IsAbsolutePath() && p.IsPrimPath();
}
bool valid_transform(const Transform& transform){for(auto value:transform.matrix)if(!std::isfinite(value))return false;return true;}
GfMatrix4d matrix(const Transform& transform){GfMatrix4d result;for(int r=0;r<4;++r)for(int c=0;c<4;++c)result[r][c]=transform.matrix[r*4+c];return result;}
Transform transform(const UsdGeomXformable& xform,UsdTimeCode time){
    GfMatrix4d m(1);bool reset=false;xform.GetLocalTransformation(&m,&reset,time);
    Transform result;for(int r=0;r<4;++r)for(int c=0;c<4;++c)result.matrix[r*4+c]=m[r][c];return result;
}
bool set_transform(const UsdGeomXformable& xform,const Transform& value,UsdTimeCode time){
    auto attr=xform.GetPrim().GetAttribute(TfToken("xformOp:transform"));
    auto op=attr?UsdGeomXformOp(attr):xform.MakeMatrixXform();return op && op.Set(matrix(value),time);
}
template<int N> using Vec = std::conditional_t<N==3,GfVec3f,GfVec2f>;
template<int N> VtArray<Vec<N>> vectors(const std::vector<std::array<float,N>>& source){
    VtArray<Vec<N>> result(source.size());for(std::size_t i=0;i<source.size();++i)for(int c=0;c<N;++c)result[i][c]=source[i][c];return result;
}
template<int N> std::vector<std::array<float,N>> vectors(const VtArray<Vec<N>>& source){
    std::vector<std::array<float,N>> result(source.size());for(std::size_t i=0;i<source.size();++i)for(int c=0;c<N;++c)result[i][c]=source[i][c];return result;
}
template<std::size_t N> bool finite(const std::vector<std::array<float,N>>& values){for(const auto& v:values)for(auto c:v)if(!std::isfinite(c))return false;return true;}
Result<void> check_stage(const UsdStageRefPtr& stage,Limits limits){
    std::size_t prims=0,vertices=0,indices=0;
    for(const auto& prim:stage->Traverse()){
        if(++prims>limits.prims)return limit_error("USD prim count exceeds budget");
        VtVec3fArray points;
        if(auto mesh=UsdGeomMesh(prim)){
            mesh.GetPointsAttr().Get(&points);VtIntArray index;mesh.GetFaceVertexIndicesAttr().Get(&index);
            if(index.size()>limits.indices-indices)return limit_error("USD topology exceeds index budget");indices+=index.size();
        }else if(auto cloud=UsdGeomPoints(prim))cloud.GetPointsAttr().Get(&points);
        if(points.size()>limits.vertices-vertices)return limit_error("USD geometry exceeds vertex budget");vertices+=points.size();
    }
    return {};
}
Result<void> capacity(const UsdStageRefPtr& stage,Limits limits,const std::vector<std::string>& paths){
    std::set<SdfPath> missing;std::size_t count=0;
    for(const auto& prim:stage->Traverse()){(void)prim;if(++count>limits.prims)return limit_error("USD prim count exceeds budget");}
    for(const auto& path:paths){
        if(!valid_path(path))return invalid("Expected an absolute USD prim path");
        for(auto p=SdfPath(path);!p.IsEmpty() && p!=SdfPath::AbsoluteRootPath();p=p.GetParentPath())
            if(!stage->GetPrimAtPath(p))missing.insert(p);
    }
    if(missing.size()>limits.prims-count)return limit_error("New USD prims exceed budget");return {};
}
struct Scratch final {std::filesystem::path path;~Scratch(){std::error_code ec;if(!path.empty())std::filesystem::remove_all(path,ec);}};
}
struct Document::Impl final {UsdStageRefPtr stage;Limits limits;};
std::string backend_version(){return std::to_string(PXR_MAJOR_VERSION)+"."+std::to_string(PXR_MINOR_VERSION)+"."+std::to_string(PXR_PATCH_VERSION);}
Result<Document> Document::create(StageMetadata metadata,Limits limits)try{
    if(!std::isfinite(metadata.metres_per_unit)||metadata.metres_per_unit<=0 || !std::isfinite(metadata.time_codes_per_second)||metadata.time_codes_per_second<=0 ||
       !std::isfinite(metadata.start_time)||!std::isfinite(metadata.end_time)||metadata.end_time<metadata.start_time ||
       (metadata.up_axis!=UpAxis::y && metadata.up_axis!=UpAxis::z))return invalid("Invalid USD stage metadata");
    if(limits.prims<1)return limit_error("Stage requires a root prim");
    TfErrorMark mark;auto stage=UsdStage::CreateInMemory();if(!stage)return failed("Cannot create OpenUSD stage",mark);
    UsdGeomSetStageMetersPerUnit(stage,metadata.metres_per_unit);UsdGeomSetStageUpAxis(stage,metadata.up_axis==UpAxis::y?UsdGeomTokens->y:UsdGeomTokens->z);
    stage->SetTimeCodesPerSecond(metadata.time_codes_per_second);stage->SetStartTimeCode(metadata.start_time);stage->SetEndTimeCode(metadata.end_time);
    auto world=UsdGeomXform::Define(stage,SdfPath("/World"));stage->SetDefaultPrim(world.GetPrim());
    if(!mark.IsClean())return failed("Cannot configure OpenUSD stage",mark);
    return Document(std::make_shared<Impl>(Impl{stage,limits}));
}catch(const std::bad_alloc&){return limit_error("USD stage allocation failed");}
Result<Document> Document::from_usda(std::string_view text,Limits limits)try{
    if(text.empty())return invalid("Empty USDA input");if(text.size()>limits.input_bytes)return limit_error("USDA input exceeds byte budget");
    TfErrorMark mark;auto layer=SdfLayer::CreateAnonymous("iiVisualCodec.usda");
    if(!layer || !layer->ImportFromString(std::string(text)))return failed("Cannot parse USDA layer",mark);
    auto stage=UsdStage::Open(layer,UsdStage::LoadNone);if(!stage || !mark.IsClean())return failed("Cannot compose USDA stage",mark);
    auto checked=check_stage(stage,limits);if(!checked)return std::unexpected(checked.error());
    return Document(std::make_shared<Impl>(Impl{stage,limits}));
}catch(const std::bad_alloc&){return limit_error("USDA allocation failed");}
Result<Document> Document::read(const std::filesystem::path& path,Limits limits)try{
    std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
    if(ec)return invalid("USD input is not an accessible regular file");if(size>limits.input_bytes)return limit_error("USD root file exceeds byte budget");
    TfErrorMark mark;auto stage=UsdStage::Open(utf8_path(path),UsdStage::LoadNone);
    if(!stage || !mark.IsClean())return failed("Cannot open USD stage",mark);
    auto checked=check_stage(stage,limits);if(!checked)return std::unexpected(checked.error());
    return Document(std::make_shared<Impl>(Impl{stage,limits}));
}catch(const std::bad_alloc&){return limit_error("USD input allocation failed");}
Result<std::string> Document::to_usda()const try{
    TfErrorMark mark;std::string result;if(!impl_->stage->GetRootLayer()->ExportToString(&result) || !mark.IsClean())return failed("Cannot serialize USDA",mark);
    if(result.size()>impl_->limits.output_bytes)return limit_error("USDA output exceeds byte budget");return result;
}catch(const std::bad_alloc&){return limit_error("USDA output allocation failed");}
Result<StageMetadata> Document::metadata()const {
    return StageMetadata{UsdGeomGetStageMetersPerUnit(impl_->stage),UsdGeomGetStageUpAxis(impl_->stage)==UsdGeomTokens->z?UpAxis::z:UpAxis::y,
                         impl_->stage->GetTimeCodesPerSecond(),impl_->stage->GetStartTimeCode(),impl_->stage->GetEndTimeCode()};
}
Result<void> Document::add_mesh(const Mesh& value,std::optional<double> time)try{
    if(!valid_path(value.path)||!valid_transform(value.transform)||!valid_time(time)||!finite(value.points)||!finite(value.normals)||!finite(value.texcoords))return invalid("Invalid USD mesh path, transform, time or coordinates");
    if(value.points.empty()||value.face_vertex_counts.empty())return invalid("Mesh requires points and faces");
    if(value.points.size()>impl_->limits.vertices || value.face_vertex_indices.size()>impl_->limits.indices)return limit_error("Mesh exceeds geometry budget");
    std::size_t count=0;for(auto n:value.face_vertex_counts){if(n<3||std::size_t(n)>value.face_vertex_indices.size()-count)return invalid("Invalid polygon topology");count+=n;}
    if(count!=value.face_vertex_indices.size())return invalid("Polygon counts do not match indices");
    for(auto index:value.face_vertex_indices)if(index<0||std::size_t(index)>=value.points.size())return invalid("Mesh vertex index is out of range");
    if((!value.normals.empty()&&value.normals.size()!=value.points.size())||(!value.texcoords.empty()&&value.texcoords.size()!=value.points.size()))return invalid("Mesh normals and UVs require vertex interpolation");
    if(!value.material_path.empty()&&(!valid_path(value.material_path)||!UsdShadeMaterial(impl_->stage->GetPrimAtPath(SdfPath(value.material_path)))))return invalid("Mesh material does not exist");
    auto cap=capacity(impl_->stage,impl_->limits,{value.path});if(!cap)return cap;
    auto existing=impl_->stage->GetPrimAtPath(SdfPath(value.path));if(existing&&!existing.IsA<UsdGeomMesh>())return invalid("Mesh path has a different schema");
    TfErrorMark mark;auto mesh=UsdGeomMesh::Define(impl_->stage,SdfPath(value.path));auto at=time_code(time);
    mesh.CreatePointsAttr().Set(vectors<3>(value.points),at);
    mesh.CreateFaceVertexCountsAttr().Set(VtIntArray(value.face_vertex_counts.begin(),value.face_vertex_counts.end()),at);
    mesh.CreateFaceVertexIndicesAttr().Set(VtIntArray(value.face_vertex_indices.begin(),value.face_vertex_indices.end()),at);
    mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
    if(!value.normals.empty()){mesh.CreateNormalsAttr().Set(vectors<3>(value.normals),at);mesh.SetNormalsInterpolation(UsdGeomTokens->vertex);}
    if(!value.texcoords.empty())UsdGeomPrimvarsAPI(mesh).CreatePrimvar(TfToken("st"),SdfValueTypeNames->TexCoord2fArray,UsdGeomTokens->vertex).Set(vectors<2>(value.texcoords),at);
    set_transform(mesh,value.transform,at);
    if(!value.material_path.empty())UsdShadeMaterialBindingAPI::Apply(mesh.GetPrim()).Bind(UsdShadeMaterial(impl_->stage->GetPrimAtPath(SdfPath(value.material_path))));
    if(!mark.IsClean())return failed("Cannot author USD mesh",mark);return {};
}catch(const std::bad_alloc&){return limit_error("USD mesh allocation failed");}
Result<void> Document::add_camera(const Camera& value,std::optional<double> time)try{
    if(!valid_path(value.path)||!valid_transform(value.transform)||!valid_time(time)||!std::isfinite(value.focal_length)||!std::isfinite(value.horizontal_aperture)||
       !std::isfinite(value.vertical_aperture)||!std::isfinite(value.near_plane)||!std::isfinite(value.far_plane)||value.focal_length<=0||
       value.horizontal_aperture<=0||value.vertical_aperture<=0||value.near_plane<=0||value.far_plane<=value.near_plane || value.far_plane>std::numeric_limits<float>::max())return invalid("Invalid USD camera parameters");
    auto cap=capacity(impl_->stage,impl_->limits,{value.path});if(!cap)return cap;
    auto existing=impl_->stage->GetPrimAtPath(SdfPath(value.path));if(existing&&!existing.IsA<UsdGeomCamera>())return invalid("Camera path has a different schema");
    TfErrorMark mark;auto camera=UsdGeomCamera::Define(impl_->stage,SdfPath(value.path));auto at=time_code(time);
    camera.CreateFocalLengthAttr().Set(float(value.focal_length),at);camera.CreateHorizontalApertureAttr().Set(float(value.horizontal_aperture),at);
    camera.CreateVerticalApertureAttr().Set(float(value.vertical_aperture),at);camera.CreateClippingRangeAttr().Set(GfVec2f(value.near_plane,value.far_plane),at);
    set_transform(camera,value.transform,at);if(!mark.IsClean())return failed("Cannot author USD camera",mark);return {};
}catch(const std::bad_alloc&){return limit_error("USD camera allocation failed");}
Result<void> Document::add_material(const Material& value)try{
    if(!valid_path(value.path))return invalid("Invalid material prim path");
    for(auto c:value.diffuse)if(!std::isfinite(c)||c<0||c>1)return invalid("Material diffuse colour must be finite and within 0..1");
    for(auto c:{value.opacity,value.roughness,value.metallic})if(!std::isfinite(c)||c<0||c>1)return invalid("Material scalars must be finite and within 0..1");
    auto cap=capacity(impl_->stage,impl_->limits,{value.path+"/Surface",value.diffuse_texture.empty()?value.path:value.path+"/Texture",value.diffuse_texture.empty()?value.path:value.path+"/UV"});if(!cap)return cap;
    auto existing=impl_->stage->GetPrimAtPath(SdfPath(value.path));if(existing&&!existing.IsA<UsdShadeMaterial>())return invalid("Material path has a different schema");
    TfErrorMark mark;auto material=UsdShadeMaterial::Define(impl_->stage,SdfPath(value.path));
    auto shader=UsdShadeShader::Define(impl_->stage,SdfPath(value.path+"/Surface"));shader.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")));
    shader.CreateInput(TfToken("diffuseColor"),SdfValueTypeNames->Color3f).Set(GfVec3f(value.diffuse[0],value.diffuse[1],value.diffuse[2]));
    shader.CreateInput(TfToken("opacity"),SdfValueTypeNames->Float).Set(value.opacity);
    shader.CreateInput(TfToken("roughness"),SdfValueTypeNames->Float).Set(value.roughness);shader.CreateInput(TfToken("metallic"),SdfValueTypeNames->Float).Set(value.metallic);
    material.CreateSurfaceOutput().ConnectToSource(shader.ConnectableAPI(),TfToken("surface"));
    if(!value.diffuse_texture.empty()){
        auto texture=UsdShadeShader::Define(impl_->stage,SdfPath(value.path+"/Texture"));texture.CreateIdAttr(VtValue(TfToken("UsdUVTexture")));
        texture.CreateInput(TfToken("file"),SdfValueTypeNames->Asset).Set(SdfAssetPath(value.diffuse_texture));
        auto uv=UsdShadeShader::Define(impl_->stage,SdfPath(value.path+"/UV"));uv.CreateIdAttr(VtValue(TfToken("UsdPrimvarReader_float2")));
        uv.CreateInput(TfToken("varname"),SdfValueTypeNames->Token).Set(TfToken("st"));
        uv.CreateOutput(TfToken("result"),SdfValueTypeNames->Float2);
        texture.CreateInput(TfToken("st"),SdfValueTypeNames->Float2).ConnectToSource(uv.ConnectableAPI(),TfToken("result"));
        texture.CreateOutput(TfToken("rgb"),SdfValueTypeNames->Float3);
        shader.GetInput(TfToken("diffuseColor")).ConnectToSource(texture.ConnectableAPI(),TfToken("rgb"));
    }
    if(!mark.IsClean())return failed("Cannot author USD Preview Surface material",mark);return {};
}catch(const std::bad_alloc&){return limit_error("USD material allocation failed");}
Result<void> Document::add_points(const Points& value,std::optional<double> time)try{
    if(!valid_path(value.path)||!valid_transform(value.transform)||!valid_time(time)||!finite(value.points))return invalid("Invalid USD point cloud");
    if(value.points.size()>impl_->limits.vertices)return limit_error("Point cloud exceeds vertex budget");
    auto cap=capacity(impl_->stage,impl_->limits,{value.path});if(!cap)return cap;
    auto existing=impl_->stage->GetPrimAtPath(SdfPath(value.path));if(existing&&!existing.IsA<UsdGeomPoints>())return invalid("Point cloud path has a different schema");
    TfErrorMark mark;auto points=UsdGeomPoints::Define(impl_->stage,SdfPath(value.path));auto at=time_code(time);
    points.CreatePointsAttr().Set(vectors<3>(value.points),at);set_transform(points,value.transform,at);
    if(!mark.IsClean())return failed("Cannot author USD point cloud",mark);return {};
}catch(const std::bad_alloc&){return limit_error("USD point cloud allocation failed");}
Result<std::vector<Mesh>> Document::meshes(std::optional<double> time)const try{
    if(!valid_time(time))return invalid("Invalid USD sample time");TfErrorMark mark;std::vector<Mesh> result;std::size_t vertices=0,indices=0;
    for(auto prim:impl_->stage->Traverse())if(auto mesh=UsdGeomMesh(prim)){
        Mesh value;value.path=prim.GetPath().GetString();VtVec3fArray points,normals;VtIntArray counts,index;VtVec2fArray uv;
        mesh.GetPointsAttr().Get(&points,time_code(time));mesh.GetFaceVertexCountsAttr().Get(&counts,time_code(time));mesh.GetFaceVertexIndicesAttr().Get(&index,time_code(time));
        if(points.size()>impl_->limits.vertices-vertices || index.size()>impl_->limits.indices-indices)return limit_error("USD mesh query exceeds geometry budget");vertices+=points.size();indices+=index.size();
        value.points=vectors<3>(points);value.face_vertex_counts.assign(counts.begin(),counts.end());value.face_vertex_indices.assign(index.begin(),index.end());
        mesh.GetNormalsAttr().Get(&normals,time_code(time));value.normals=vectors<3>(normals);
        auto st=UsdGeomPrimvarsAPI(mesh).GetPrimvar(TfToken("st"));if(st)st.ComputeFlattened(&uv,time_code(time));value.texcoords=vectors<2>(uv);
        value.transform=transform(mesh,time_code(time));
        auto material=UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial();if(material)value.material_path=material.GetPath().GetString();
        result.push_back(std::move(value));
    }
    if(!mark.IsClean())return failed("Cannot read USD meshes",mark);return result;
}catch(const std::bad_alloc&){return limit_error("USD mesh query allocation failed");}
Result<std::vector<Camera>> Document::cameras(std::optional<double> time)const try{
    if(!valid_time(time))return invalid("Invalid USD sample time");TfErrorMark mark;std::vector<Camera> result;
    for(auto prim:impl_->stage->Traverse())if(auto camera=UsdGeomCamera(prim)){
        Camera value;value.path=prim.GetPath().GetString();value.transform=transform(camera,time_code(time));float f=0;GfVec2f range;
        camera.GetFocalLengthAttr().Get(&f,time_code(time));value.focal_length=f;camera.GetHorizontalApertureAttr().Get(&f,time_code(time));value.horizontal_aperture=f;
        camera.GetVerticalApertureAttr().Get(&f,time_code(time));value.vertical_aperture=f;camera.GetClippingRangeAttr().Get(&range,time_code(time));value.near_plane=range[0];value.far_plane=range[1];result.push_back(value);
    }
    if(!mark.IsClean())return failed("Cannot read USD cameras",mark);return result;
}catch(const std::bad_alloc&){return limit_error("USD camera query allocation failed");}
Result<std::vector<Points>> Document::point_clouds(std::optional<double> time)const try{
    if(!valid_time(time))return invalid("Invalid USD sample time");TfErrorMark mark;std::vector<Points> result;std::size_t vertices=0;
    for(auto prim:impl_->stage->Traverse())if(auto cloud=UsdGeomPoints(prim)){
        VtVec3fArray points;cloud.GetPointsAttr().Get(&points,time_code(time));if(points.size()>impl_->limits.vertices-vertices)return limit_error("USD points query exceeds vertex budget");vertices+=points.size();
        result.push_back({prim.GetPath().GetString(),vectors<3>(points),transform(cloud,time_code(time))});
    }
    if(!mark.IsClean())return failed("Cannot read USD point clouds",mark);return result;
}catch(const std::bad_alloc&){return limit_error("USD points query allocation failed");}
Result<std::vector<Material>> Document::materials()const try{
    TfErrorMark mark;std::vector<Material> result;
    for(auto prim:impl_->stage->Traverse())if(auto material=UsdShadeMaterial(prim)){
        auto shader=material.ComputeSurfaceSource();TfToken id;
        if(!shader || !shader.GetIdAttr().Get(&id) || id!=TfToken("UsdPreviewSurface"))continue;
        Material value;value.path=prim.GetPath().GetString();GfVec3f diffuse(0.5f);
        auto colour=shader.GetInput(TfToken("diffuseColor"));if(colour)colour.Get(&diffuse);
        for(unsigned c=0;c<3;++c)value.diffuse[c]=diffuse[c];
        for(auto entry:{std::pair{"opacity",&value.opacity},std::pair{"roughness",&value.roughness},std::pair{"metallic",&value.metallic}}){
            auto input=shader.GetInput(TfToken(entry.first));if(input)input.Get(entry.second);
        }
        UsdShadeConnectableAPI source;TfToken output;UsdShadeAttributeType type;
        if(colour && colour.GetConnectedSource(&source,&output,&type)){
            auto texture=UsdShadeShader(source.GetPrim());SdfAssetPath asset;
            if(texture){auto file=texture.GetInput(TfToken("file"));if(file && file.Get(&asset))value.diffuse_texture=asset.GetAssetPath();}
        }
        result.push_back(std::move(value));
    }
    if(!mark.IsClean())return failed("Cannot read USD Preview Surface materials",mark);return result;
}catch(const std::bad_alloc&){return limit_error("USD material query allocation failed");}
Result<void> Document::write(const std::filesystem::path& path,Format format)const try{
    if(path.empty()||(format!=Format::usda&&format!=Format::usdc&&format!=Format::usdz))return invalid("Invalid USD output path or format");
    const auto extension=format==Format::usda?".usda":format==Format::usdc?".usdc":".usdz";
    if(path.extension()!=extension)return invalid("USD output extension must match its format");
    std::error_code ec;if(std::filesystem::exists(path,ec)||ec)return invalid("USD destination already exists or cannot be inspected");
    auto parent=path.parent_path();if(parent.empty())parent=".";
    if(!std::filesystem::is_directory(parent,ec)||ec)return invalid("USD destination parent does not exist");
    static std::atomic<std::uint64_t> sequence{0};Scratch scratch;
    for(unsigned attempt=0;attempt<10;++attempt){
        auto candidate=parent/(".iivisual-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence++));
        if(std::filesystem::create_directory(candidate,ec)){scratch.path=candidate;break;}if(ec)return invalid("Cannot create USD export workspace");
    }
    if(scratch.path.empty())return invalid("Cannot reserve USD export workspace");
    const auto temporary=scratch.path/("scene"+std::string(extension));TfErrorMark mark;
    if(format==Format::usdz){
        const auto root=scratch.path/"root.usdc";auto layer=impl_->stage->Flatten();
        if(!layer||!layer->Export(utf8_path(root))||!UsdUtilsCreateNewUsdzPackage(SdfAssetPath(utf8_path(root)),utf8_path(temporary)))return failed("Cannot package USDZ assets",mark);
    }else if(!impl_->stage->GetRootLayer()->Export(utf8_path(temporary)))return failed("Cannot export USD layer",mark);
    if(!mark.IsClean())return failed("USD export failed",mark);
    const auto size=std::filesystem::file_size(temporary,ec);if(ec)return invalid("Cannot inspect USD output");if(size>impl_->limits.output_bytes)return limit_error("USD export exceeds byte budget");
    if(!std::filesystem::copy_file(temporary,path,std::filesystem::copy_options::none,ec)||ec)return invalid("Cannot publish new USD file");
    return {};
}catch(const std::bad_alloc&){return limit_error("USD export allocation failed");}catch(const std::filesystem::filesystem_error& error){return invalid(error.what());}
}
