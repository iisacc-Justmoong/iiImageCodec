#include <iiVisualCodec/Spatial.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace ii::visual;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while(false)
int main() {
    image::Image left{2,2,{255,0,0,255, 255,0,0,255, 255,0,0,255, 255,0,0,255}};
    image::Image right=left; for(std::size_t i=0;i<right.rgba.size();i+=4) {right.rgba[i]=0;right.rgba[i+2]=255;}
    for(auto layout:{spatial::StereoLayout::side_by_side, spatial::StereoLayout::top_bottom}) {
        auto packed=spatial::pack_stereo(left.view(),right.view(),layout); CHECK(packed);
        auto l=spatial::extract_eye(packed->view(),layout,spatial::Eye::left);
        auto r=spatial::extract_eye(packed->view(),layout,spatial::Eye::right);
        CHECK(l && r && l->rgba==left.rgba && r->rgba==right.rgba);
    }
    CHECK(!spatial::pack_stereo(left.view(),right.view(),spatial::StereoLayout::mono));
    CHECK(!spatial::extract_eye({3,2,image::PixelFormat::rgba8,0,left.rgba},spatial::StereoLayout::side_by_side,spatial::Eye::left));
    // Each eye is a complete 2:1 panorama. Distinct colours detect sampling
    // across the packed-eye boundary and wrapping into the opposite eye.
    left.width=right.width=4;left.rgba.resize(32);right.rgba.resize(32);
    for(std::size_t i=0;i<32;i+=4) { left.rgba[i]=255;left.rgba[i+3]=255;right.rgba[i+2]=255;right.rgba[i+3]=255; }
    auto packed=spatial::pack_stereo(left.view(),right.view(),spatial::StereoLayout::top_bottom);CHECK(packed);
    spatial::Metadata metadata{spatial::Projection::equirectangular,spatial::StereoLayout::top_bottom};
    spatial::ViewOptions view;view.width=3;view.height=3;
    auto viewport=spatial::render_view(packed->view(),metadata,view);CHECK(viewport);
    CHECK(viewport->rgba[16]==255 && viewport->rgba[18]==0);
    view.eye=spatial::Eye::right;viewport=spatial::render_view(packed->view(),metadata,view);CHECK(viewport);
    CHECK(viewport->rgba[16]==0 && viewport->rgba[18]==255);
    // +Y 180 degrees rotates the central ray to the longitude seam.
    view.orientation.rotation={0,1,0,0};CHECK(spatial::render_view(packed->view(),metadata,view));
    image::Image grid{8,4,std::vector<std::uint8_t>(8*4*4)};
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<8;++x){grid.rgba[(y*8+x)*4]=x*32;grid.rgba[(y*8+x)*4+3]=255;}
    spatial::ViewOptions centre;centre.width=centre.height=1;
    auto forward=spatial::render_view(grid.view(),{spatial::Projection::equirectangular},centre);CHECK(forward && forward->rgba[0]==112);
    centre.orientation.rotation={0,std::sqrt(0.5),0,std::sqrt(0.5)};
    auto turned=spatial::render_view(grid.view(),{spatial::Projection::equirectangular},centre);CHECK(turned && turned->rgba[0]==48);
    view.horizontal_fov_degrees=180;CHECK(!spatial::render_view(packed->view(),metadata,view));
    metadata.pose.rotation={0,0,0,0};CHECK(!spatial::validate(metadata));
    spatial::CameraIntrinsics camera{2,2,2,2,0.5,0.5};
    const std::vector<float> depth{2,0,std::numeric_limits<float>::quiet_NaN(),4};
    spatial::Pose pose;pose.translation={10,20,30};
    auto points=spatial::unproject_depth({2,2,0,depth},camera,pose);CHECK(points && points->points.size()==2);
    CHECK((points->points[0]==std::array<double,3>{9.5,20.5,28}));
    CHECK((points->points[1]==std::array<double,3>{11,19,26}));
    camera.fx=0;CHECK(!spatial::unproject_depth({2,2,0,depth},camera));
    image::Limits limit;limit.pixel_bytes=4;camera.fx=2;CHECK(!spatial::unproject_depth({2,2,0,depth},camera,{},limit));
    CHECK(!spatial::unproject_depth({2,2,std::numeric_limits<std::size_t>::max(),depth},camera));
    std::cout << "spatial contracts passed: stereo, 360 viewport, AR depth and bounds\n";
}
