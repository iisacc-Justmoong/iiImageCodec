#include <iiVisualCodec/OpenCVBridge.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <future>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
using namespace ii::visual;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while(false)
int main(int argc,char** argv){
    namespace v=vision;namespace cvb=opencv;
    std::vector<std::uint8_t> padded{255,0,0,255,0,0, 99,99, 0,0,255,0,0,255};
    image::ImageView rgb{2,2,image::PixelFormat::rgb8,8,padded};
    auto native=cvb::to_mat(rgb,cvb::PixelOrder::bgr);CHECK(native && native->at<cv::Vec3b>(0,0)[2]==255);
    padded[0]=0;CHECK(native->at<cv::Vec3b>(0,0)[2]==255);padded[0]=255;
    cv::Mat larger(4,4,CV_8UC3,cv::Scalar(0,0,255));auto roi=larger(cv::Rect(1,1,2,2));CHECK(!roi.isContinuous());
    auto owned=cvb::from_mat(roi,cvb::PixelOrder::bgr);CHECK(owned && owned->rgba[0]==255);larger.setTo(cv::Scalar(0,0,0));CHECK(owned->rgba[0]==255);
    CHECK(!cvb::from_mat(cv::Mat(2,2,CV_32F),cvb::PixelOrder::gray));
    auto resized=cvb::resize(rgb,4,4,cvb::Interpolation::nearest);CHECK(resized && resized->width==4 && resized->rgba[0]==255);
    auto gray=cvb::grayscale(rgb);CHECK(gray && gray->rgba[0]==gray->rgba[1] && gray->rgba[1]==gray->rgba[2]);
    image::Image shape{32,32,std::vector<std::uint8_t>(32*32*4,0)};
    for(unsigned y=8;y<24;++y)for(unsigned x=8;x<24;++x)for(unsigned c=0;c<4;++c)shape.rgba[(y*32+x)*4+c]=255;
    auto edges=cvb::edges(shape.view());CHECK(edges && std::count(edges->values.begin(),edges->values.end(),255)>0);
    v::Mask mask{8,6,std::vector<std::uint8_t>(8*6)};
    for(unsigned y=1;y<3;++y)for(unsigned x=1;x<4;++x)mask.values[y*8+x]=255;mask.values[4*8+6]=255;
    auto regions=cvb::regions(mask,{2,0,8});CHECK(regions && regions->size()==1 && regions->front().box.x==1 && regions->front().box.width==3);
    auto outlined=cvb::draw_objects(shape.view(),*regions);CHECK(outlined && outlined->rgba[(1*32+1)*4+1]==255);
    image::Image red{2,2,{255,0,0,255,255,0,0,255,255,0,0,255,255,0,0,255}};
    image::Image blue=red;for(std::size_t i=0;i<blue.rgba.size();i+=4){blue.rgba[i]=0;blue.rgba[i+2]=255;}
    auto transparent=red;transparent.rgba[3]=37;auto gray_alpha=cvb::grayscale(transparent.view());CHECK(gray_alpha && gray_alpha->rgba[3]==37);
    auto preprocessing=std::make_shared<cvb::Preprocessor>();v::PreprocessOptions options;options.width=options.height=2;
    auto prepared=preprocessing->prepare(red.view(),options);CHECK(prepared && prepared->tensor.values[0]==1 && prepared->tensor.values[8]==0);
    options.channels=v::ChannelOrder::bgr;auto bgr=preprocessing->prepare(red.view(),options);CHECK(bgr && bgr->tensor.values[0]==0 && bgr->tensor.values[8]==1);
    image::Image wide{4,2,std::vector<std::uint8_t>(4*2*4,255)};options={};options.width=options.height=4;
    auto boxed=preprocessing->prepare(wide.view(),options);CHECK(boxed && boxed->transform.pad_y==1 && boxed->transform.scale_x==1);
    CHECK(std::abs(boxed->tensor.values[0]-114.0f/255)<1e-6 && boxed->tensor.values[4]==1);
    image::Image noise{128,128,std::vector<std::uint8_t>(128*128*4)};std::uint32_t state=42;
    for(std::size_t i=0;i<noise.rgba.size();i+=4){state=state*1664525+1013904223;for(unsigned c=0;c<3;++c)noise.rgba[i+c]=std::uint8_t(state>>24);noise.rgba[i+3]=255;}
    auto features=cvb::orb_features(noise.view(),100);CHECK(features && !features->keypoints.empty() && features->descriptors.size()==features->keypoints.size()*32);
    image::Image shifted{128,128,std::vector<std::uint8_t>(128*128*4)};
    for(unsigned y=1;y<128;++y)for(unsigned x=2;x<128;++x)std::copy_n(noise.rgba.data()+((y-1)*128+x-2)*4,4,shifted.rgba.data()+(y*128+x)*4);
    const std::vector<v::Point> points{{32,32},{64,64},{96,96}};auto flow=cvb::track_features(noise.view(),shifted.view(),points);CHECK(flow && flow->size()==3);
    unsigned tracked=0;for(unsigned i=0;i<3;++i)if((*flow)[i].found){++tracked;CHECK(std::abs((*flow)[i].point.x-points[i].x-2)<.4 && std::abs((*flow)[i].point.y-points[i].y-1)<.4);}CHECK(tracked>=2);
    CHECK(!cvb::resize(rgb,0,1));CHECK(!cvb::edges(rgb,{5,1,3,false}));CHECK(!cvb::regions(mask,{1,0,6}));
    if(argc==2) {
        std::filesystem::create_directories(argv[1]);
        auto png=image::encode_png(red.view());CHECK(png);
        std::ofstream out(std::filesystem::path(argv[1])/"red.png",std::ios::binary);
        out.write(reinterpret_cast<const char*>(png->data()),png->size());CHECK(out.good());
    }
    std::cout<<"OpenCV "<<cvb::backend_version()<<" contracts passed: owned Mat/ROI, preprocessing, ORB and optical flow\n";
}
