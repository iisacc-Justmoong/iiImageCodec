#include <iiImageCodec/ImageCodec.h>
#include <iostream>
#include <stdexcept>
namespace { void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); } }
int main() {
    try {
        ii::image::Image source{4, 2, {
            255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255,
            255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255}};
        source.metadata.icc_profile={4,3,2,1}; source.metadata.dots_per_meter_x=3780;
        auto square=ii::image::thumbnail(source.view(),2,false);
        check(square&&square->rgba==std::vector<std::uint8_t>({0,255,0,255,0,0,255,255,0,255,0,255,0,0,255,255}),"Center crop or row order is incorrect");
        check(square->metadata.icc_profile==source.metadata.icc_profile&&square->metadata.dots_per_meter_x==3780,"Thumbnail lost source metadata");
        auto circle=ii::image::thumbnail(source.view(),8,true);
        check(circle&&circle->rgba[3]==0&&circle->rgba[(4*8+4)*4+3]==255,"Circular coverage is incorrect");
        ii::image::Image transparent{2,1,{255,0,0,0,0,0,255,255}};
        auto filtered=ii::image::thumbnail(transparent.view(),1,false);
        check(filtered&&filtered->rgba[0]==0&&filtered->rgba[2]==255&&filtered->rgba[3]>=127&&filtered->rgba[3]<=128,"Filtering introduced a transparent-color fringe");
        check(!ii::image::thumbnail(source.view(),0)&&!ii::image::thumbnail(source.view(),40000),"Invalid thumbnail dimensions accepted");
        source.metadata.orientation=6;
        check(!ii::image::thumbnail(source.view(),8),"Unnormalized orientation was silently discarded");
        std::cout<<"THUMBNAIL_OK center=true circle=true alpha=true metadata=true bounded=true\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
