#include "CadCodec.h"
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <DESTEP_Parameters.hxx>
#include <GProp_GProps.hxx>
#include <IGESControl_Reader.hxx>
#include <IGESControl_Writer.hxx>
#include <IGESData_IGESModel.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <StepData_StepModel.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <TopoDS.hxx>
#include <gp_GTrsf.hxx>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <numbers>
#include <sstream>
#include <streambuf>

namespace ii::visual::cad {
namespace {
struct Failure {image::Error error;};
[[noreturn]] void fail(image::ErrorCode code,std::string message){throw Failure{{code,std::move(message)}};}
[[noreturn]] void invalid(std::string s){fail(image::ErrorCode::invalid_input,std::move(s));}
[[noreturn]] void limit(){fail(image::ErrorCode::limit_exceeded,"CAD budget exceeded");}
void positive(double value){if(!std::isfinite(value)||value<=1e-7||value>1e12)invalid("CAD length must be finite, above tolerance and at most 1e12 mm");}
std::mutex exchange_mutex;
class Output final:public std::streambuf {
public:
    explicit Output(std::size_t maximum):maximum_(maximum){}
    std::vector<std::uint8_t> bytes;bool exceeded=false;
protected:
    std::streamsize xsputn(const char* p,std::streamsize count) override {
        if(count<0||static_cast<std::size_t>(count)>maximum_-bytes.size()){exceeded=true;return 0;}
        bytes.insert(bytes.end(),p,p+count);return count;
    }
    int_type overflow(int_type c) override {
        if(traits_type::eq_int_type(c,traits_type::eof()))return traits_type::not_eof(c);
        char b=traits_type::to_char_type(c);return xsputn(&b,1)==1?c:traits_type::eof();
    }
private:std::size_t maximum_;
};
std::size_t count(const TopoDS_Shape& shape,TopAbs_ShapeEnum kind){NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> map;TopExp::MapShapes(shape,kind,map);return static_cast<std::size_t>(map.Extent());}
template<class Operation> TopoDS_Shape boolean(const TopoDS_Shape& a,const TopoDS_Shape& b) {
    Operation operation;NCollection_List<TopoDS_Shape> args,tools;args.Append(a);tools.Append(b);
    operation.SetArguments(args);operation.SetTools(tools);operation.SetNonDestructive(true);operation.SetRunParallel(false);operation.Build();
    if(!operation.IsDone()||operation.HasErrors())fail(image::ErrorCode::codec_failure,"CAD Boolean operation failed");return operation.Shape();
}
}
#define CAD_CATCH catch(const Failure& e){return std::unexpected(e.error);} \
 catch(const Standard_Failure& e){return std::unexpected(image::Error{image::ErrorCode::codec_failure,e.what()});} \
 catch(const std::bad_alloc&){return std::unexpected(image::Error{image::ErrorCode::limit_exceeded,"CAD allocation failed"});} \
 catch(const std::exception& e){return std::unexpected(image::Error{image::ErrorCode::codec_failure,e.what()});}
struct Shape::Impl final {
    TopoDS_Shape shape;Limits limits;
    static Result<Shape> make(TopoDS_Shape shape,Limits limits) {
        if(shape.IsNull())invalid("CAD operation produced a null shape");
        NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> map;TopExp::MapShapes(shape,map);
        if(static_cast<std::size_t>(map.Extent())>limits.topology)limit();
        if(!BRepCheck_Analyzer(shape).IsValid())invalid("CAD topology is invalid");
        return Shape(std::make_shared<Impl>(Impl{std::move(shape),limits}));
    }
};
Result<Shape> Shape::box(geometry::Vec3 size,Limits limits) try {
    for(auto x:size)positive(x);return Impl::make(BRepPrimAPI_MakeBox(size[0],size[1],size[2]).Shape(),limits);
} CAD_CATCH
Result<Shape> Shape::cylinder(double radius,double height,Limits limits) try {
    positive(radius);positive(height);return Impl::make(BRepPrimAPI_MakeCylinder(radius,height).Shape(),limits);
} CAD_CATCH
Result<Shape> Shape::sphere(double radius,Limits limits) try {
    positive(radius);return Impl::make(BRepPrimAPI_MakeSphere(radius).Shape(),limits);
} CAD_CATCH
Result<Shape> Shape::extrude(std::span<const geometry::Vec2> polygon,double height,Limits limits) try {
    positive(height);if(polygon.size()<3)invalid("Extrusion requires at least three polygon vertices");
    if(polygon.size()>limits.geometry.vertices||polygon.size()>4096)limit();
    double area=0;
    for(std::size_t i=0;i<polygon.size();++i) {
        auto a=polygon[i],b=polygon[(i+1)%polygon.size()];
        for(auto x:a)if(!std::isfinite(x)||std::abs(x)>1e12)invalid("Invalid extrusion point");
        if(std::hypot(a[0]-b[0],a[1]-b[1])<=1e-7)invalid("Repeated polygon point");
        area+=a[0]*b[1]-b[0]*a[1];
    }
    if(!std::isfinite(area)||std::abs(area)<=1e-14)invalid("Degenerate extrusion polygon");
    // A bounded quadratic segment check rejects self crossing before OCCT.
    auto orient=[](auto a,auto b,auto c){return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);};
    auto on=[](auto a,auto b,auto p){return p[0]>=std::min(a[0],b[0])&&p[0]<=std::max(a[0],b[0])&&p[1]>=std::min(a[1],b[1])&&p[1]<=std::max(a[1],b[1]);};
    for(std::size_t i=0;i<polygon.size();++i)for(std::size_t j=i+1;j<polygon.size();++j) {
        auto nexti=(i+1)%polygon.size(),nextj=(j+1)%polygon.size();if(j==nexti||nextj==i)continue;
        auto a=polygon[i],b=polygon[nexti],c=polygon[j],d=polygon[nextj];
        double p=orient(a,b,c),q=orient(a,b,d),r=orient(c,d,a),s=orient(c,d,b);
        if(((p>0&&q<0)||(p<0&&q>0))&&((r>0&&s<0)||(r<0&&s>0)))invalid("Self crossing extrusion polygon");
        if((p==0&&on(a,b,c))||(q==0&&on(a,b,d))||(r==0&&on(c,d,a))||(s==0&&on(c,d,b)))invalid("Touching extrusion polygon edges");
    }
    BRepBuilderAPI_MakePolygon wire;
    if(area>0)for(auto p:polygon)wire.Add(gp_Pnt(p[0],p[1],0));
    else for(auto it=polygon.rbegin();it!=polygon.rend();++it)wire.Add(gp_Pnt((*it)[0],(*it)[1],0));
    wire.Close();if(!wire.IsDone())invalid("Cannot construct extrusion wire");
    BRepBuilderAPI_MakeFace face(wire.Wire());if(!face.IsDone())invalid("Cannot construct extrusion face");
    return Impl::make(BRepPrimAPI_MakePrism(face.Face(),gp_Vec(0,0,height)).Shape(),limits);
} CAD_CATCH
Result<Shape> Shape::decode(std::span<const std::uint8_t> bytes,Format format,Limits limits) try {
    if(bytes.empty())invalid("Empty CAD asset");if(bytes.size()>limits.geometry.input_bytes)limit();
    std::string text(reinterpret_cast<const char*>(bytes.data()),bytes.size());std::istringstream input(text);TopoDS_Shape shape;
    if(format==Format::brep){BRep_Builder builder;BRepTools::Read(shape,input,builder);}
    else {
        std::lock_guard lock(exchange_mutex);
        if(format==Format::step) {
            STEPControl_Reader reader;DESTEP_Parameters params;
            if(reader.ReadStream("memory.step",params,input)!=IFSelect_RetDone)invalid("Cannot parse STEP stream");
            reader.SetSystemLengthUnit(1.0); // native millimetres, independent of file units
            if(reader.TransferRoots()<=0)invalid("STEP contains no transferable roots");shape=reader.OneShape();
        }else if(format==Format::iges) {
            fail(image::ErrorCode::unsupported_format,"OCCT IGES input requires Shape::read with an explicit local file");
        }else fail(image::ErrorCode::unsupported_format,"Unknown CAD format");
    }
    return Impl::make(std::move(shape),limits);
} CAD_CATCH
Result<Shape> Shape::read(const std::filesystem::path& path,Format format,Limits limits) try {
    const auto size=std::filesystem::file_size(path);if(size>limits.geometry.input_bytes)limit();
    if(format!=Format::iges) {
        std::ifstream input(path,std::ios::binary);if(!input)invalid("Cannot open CAD file");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        if(!input)invalid("Cannot read CAD file");return decode(bytes,format,limits);
    }
    auto utf8=path.u8string();std::string filename(reinterpret_cast<const char*>(utf8.data()),utf8.size());
    std::lock_guard lock(exchange_mutex);IGESControl_Reader reader;
    if(reader.ReadFile(filename.c_str())!=IFSelect_RetDone)invalid("Cannot parse IGES file");
    auto section=reader.IGESModel()->GlobalSection();section.SetCascadeUnit(1.0);reader.IGESModel()->SetGlobalSection(section);
    if(reader.TransferRoots()<=0)invalid("IGES contains no transferable roots");return Impl::make(reader.OneShape(),limits);
} CAD_CATCH
Result<std::vector<std::uint8_t>> Shape::encode(Format format) const try {
    if(!impl_)invalid("Moved-from CAD shape");Output buffer(impl_->limits.geometry.output_bytes);std::ostream output(&buffer);
    if(format==Format::brep)BRepTools::Write(impl_->shape,output,false,false,TopTools_FormatVersion_CURRENT);
    else {
        std::lock_guard lock(exchange_mutex);
        if(format==Format::step) {
            STEPControl_Writer writer;DESTEP_Parameters params;params.WriteUnit=UnitsMethods_LengthUnit_Millimeter;
            writer.Model()->SetLocalLengthUnit(1.0);
            if(writer.Transfer(impl_->shape,STEPControl_AsIs,params)!=IFSelect_RetDone)fail(image::ErrorCode::codec_failure,"Cannot transfer CAD shape to STEP");
            if(writer.WriteStream(output)!=IFSelect_RetDone&&!buffer.exceeded)fail(image::ErrorCode::codec_failure,"Cannot write STEP stream");
        }else if(format==Format::iges) {
            IGESControl_Writer writer("MM",1);auto section=writer.Model()->GlobalSection();section.SetCascadeUnit(1.0);writer.Model()->SetGlobalSection(section);
            if(!writer.AddShape(impl_->shape))fail(image::ErrorCode::codec_failure,"Cannot transfer CAD shape to IGES");
            if(!writer.Write(output)&&!buffer.exceeded)fail(image::ErrorCode::codec_failure,"Cannot write IGES stream");
        }else fail(image::ErrorCode::unsupported_format,"Unknown CAD format");
    }
    if(buffer.exceeded)limit();if(!output||buffer.bytes.empty())fail(image::ErrorCode::codec_failure,"CAD stream write failed");return std::move(buffer.bytes);
} CAD_CATCH
Result<Shape> Shape::boolean_with(const Shape& other,BooleanOperation operation) const try {
    if(!impl_||!other.impl_)invalid("Moved-from CAD shape");TopoDS_Shape result;
    switch(operation) {
    case BooleanOperation::unite:result=boolean<BRepAlgoAPI_Fuse>(impl_->shape,other.impl_->shape);break;
    case BooleanOperation::subtract:result=boolean<BRepAlgoAPI_Cut>(impl_->shape,other.impl_->shape);break;
    case BooleanOperation::intersect:result=boolean<BRepAlgoAPI_Common>(impl_->shape,other.impl_->shape);break;
    default:invalid("Unknown CAD Boolean operation");
    }
    return Impl::make(std::move(result),impl_->limits);
} CAD_CATCH
Result<Shape> Shape::transformed(const geometry::Transform& transform) const try {
    if(!impl_)invalid("Moved-from CAD shape");if(auto r=geometry::validate(transform);!r)throw Failure{r.error()};
    gp_GTrsf native;for(int r=1;r<=3;++r)for(int c=1;c<=4;++c)native.SetValue(r,c,transform.matrix[(r-1)*4+c-1]);
    native.SetForm();
    if(native.Form()!=gp_Other) {
        const auto& m=transform.matrix;gp_Trsf similarity;
        similarity.SetValues(m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11]);
        BRepBuilderAPI_Transform operation(impl_->shape,similarity,true);
        if(!operation.IsDone())fail(image::ErrorCode::codec_failure,"CAD similarity transform failed");
        return Impl::make(operation.Shape(),impl_->limits);
    }
    BRepBuilderAPI_GTransform operation(impl_->shape,native,true);if(!operation.IsDone())fail(image::ErrorCode::codec_failure,"CAD transform failed");
    return Impl::make(operation.Shape(),impl_->limits);
} CAD_CATCH
Result<Properties> Shape::properties() const try {
    if(!impl_)invalid("Moved-from CAD shape");Properties result;result.valid=BRepCheck_Analyzer(impl_->shape).IsValid();
    result.vertices=count(impl_->shape,TopAbs_VERTEX);result.edges=count(impl_->shape,TopAbs_EDGE);result.faces=count(impl_->shape,TopAbs_FACE);
    result.shells=count(impl_->shape,TopAbs_SHELL);result.solids=count(impl_->shape,TopAbs_SOLID);
    GProp_GProps surface;BRepGProp::SurfaceProperties(impl_->shape,surface);result.surface_area=surface.Mass();
    auto centre=surface.CentreOfMass();
    if(result.solids){GProp_GProps volume;BRepGProp::VolumeProperties(impl_->shape,volume,true);result.volume=std::abs(volume.Mass());centre=volume.CentreOfMass();}
    result.centre_of_mass={centre.X(),centre.Y(),centre.Z()};Bnd_Box box;BRepBndLib::AddOptimal(impl_->shape,box,false,false);
    if(!box.IsVoid()){double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);result.bounds={{x0,y0,z0},{x1,y1,z1}};}
    return result;
} CAD_CATCH
Result<geometry::Mesh> Shape::tessellate(TessellationOptions options) const try {
    if(!impl_)invalid("Moved-from CAD shape");positive(options.linear_deflection);
    if(!std::isfinite(options.angular_deflection)||options.angular_deflection<=0||options.angular_deflection>=std::numbers::pi)invalid("Invalid meshing angle");
    BRepBuilderAPI_Copy copy(impl_->shape,true,false);auto shape=copy.Shape();
    BRepMesh_IncrementalMesh mesher(shape,options.linear_deflection,false,options.angular_deflection,false);
    if(!mesher.IsDone())fail(image::ErrorCode::codec_failure,"CAD tessellation failed");
    geometry::Mesh mesh;mesh.name="CAD";std::uint32_t face_id=0;const auto limits=impl_->limits.geometry;
    for(TopExp_Explorer explorer(shape,TopAbs_FACE);explorer.More();explorer.Next(),++face_id) {
        const auto face=TopoDS::Face(explorer.Current());TopLoc_Location location;const auto triangulation=BRep_Tool::Triangulation(face,location);
        if(triangulation.IsNull())continue;
        const auto vertices=static_cast<std::size_t>(triangulation->NbNodes()),triangles=static_cast<std::size_t>(triangulation->NbTriangles());
        if(vertices>limits.vertices-mesh.positions.size()||vertices>UINT32_MAX-mesh.positions.size()||triangles>(limits.indices-mesh.indices.size())/3)limit();
        const auto base=static_cast<std::uint32_t>(mesh.positions.size());
        for(int i=1;i<=triangulation->NbNodes();++i){auto p=triangulation->Node(i).Transformed(location.Transformation());mesh.positions.push_back({p.X(),p.Y(),p.Z()});}
        for(int i=1;i<=triangulation->NbTriangles();++i) {
            int a,b,c;triangulation->Triangle(i).Get(a,b,c);if(face.Orientation()==TopAbs_REVERSED)std::swap(b,c);
            mesh.indices.insert(mesh.indices.end(),{base+static_cast<std::uint32_t>(a-1),base+static_cast<std::uint32_t>(b-1),base+static_cast<std::uint32_t>(c-1)});mesh.source_faces.push_back(face_id);
        }
    }
    return geometry::with_normals(std::move(mesh),limits);
} CAD_CATCH
std::string backend_version(){return OCC_VERSION_COMPLETE;}
#undef CAD_CATCH
}
