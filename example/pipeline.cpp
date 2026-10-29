#include <iiVisualCodec/VisualCodec.h>
#include <iostream>

int main() {
    namespace visual=ii::visual;
    visual::image::Image image{16,16,std::vector<std::uint8_t>(16*16*4,255)};
    auto png=visual::image::encode_png(image.view());
    const visual::video::FrameView frame{image.view(),0,1};
    auto movie=visual::video::encode(std::span(&frame,1));
    if(!png || !movie)return 1;
    auto decoded=visual::video::decode(*movie);
    if(!decoded || decoded->frames.size()!=1)return 2;

    const std::vector<float> depth{1,2,3,4};
    const visual::spatial::CameraIntrinsics camera{2,2,2,2,0.5,0.5};
    auto cloud=visual::spatial::unproject_depth({2,2,0,depth},camera);
    auto scene=visual::usd::Document::create();
    if(!cloud || !scene)return 3;
    visual::usd::Points points;points.path="/World/Depth";
    for(const auto& p:cloud->points)points.points.push_back({float(p[0]),float(p[1]),float(p[2])});
    if(!scene->add_points(points))return 4;
    auto usda=scene->to_usda();if(!usda)return 5;
    auto restored=visual::usd::Document::from_usda(*usda);if(!restored)return 6;
    auto restored_points=restored->point_clouds();
    if(!restored_points || restored_points->size()!=1 || restored_points->front().points!=points.points)return 7;
    std::cout<<"PNG bytes="<<png->size()<<", FFV1 bytes="<<movie->size()
             <<", AR points="<<points.points.size()<<", USDA bytes="<<usda->size()<<'\n';
}
