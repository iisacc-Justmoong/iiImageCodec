#include <iiVisualCodec/VideoCodec.h>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <filesystem>
#include <fstream>
using namespace ii::visual;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while(false)
int main(int argc,char** argv) {
    const auto backend=video::backend_info();
    CHECK(backend.license.find("LGPL")!=std::string::npos);
    CHECK(backend.configuration.find("--enable-gpl")==std::string::npos);
    image::Image image{16,16,std::vector<std::uint8_t>(16*16*4)};
    for(std::size_t i=0;i<image.rgba.size();i+=4){image.rgba[i]=47;image.rgba[i+1]=81;image.rgba[i+2]=129;image.rgba[i+3]=255;}
    std::vector<video::FrameView> frames{{image.view(),0,1},{image.view(),1,1},{image.view(),3,1}};
    video::EncodeOptions options;options.time_base={1,30};
    auto bytes=video::encode(frames,options);if(!bytes)std::cerr<<bytes.error().message<<'\n';CHECK(bytes);
    auto save=[&](const char* name,const std::vector<std::uint8_t>& data){if(argc==2){std::filesystem::create_directories(argv[1]);std::ofstream out(std::filesystem::path(argv[1])/name,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),data.size());CHECK(out.good());}};
    save("lossless.mkv",*bytes);
    auto info=video::inspect(*bytes);CHECK(info && info->width==16 && info->height==16 && info->codec=="ffv1");
    auto decoded=video::decode(*bytes);if(!decoded)std::cerr<<decoded.error().message<<'\n';CHECK(decoded && decoded->frames.size()==3);
    for(const auto& frame:decoded->frames)CHECK(frame.image.rgba==image.rgba);
    CHECK(decoded->frames[0].pts==0 && decoded->frames[1].pts>0 && decoded->frames[2].pts>decoded->frames[1].pts);
    // A real MP4 interframe codec exercises delayed packets and the final flush.
    options.container=video::Container::mp4;options.codec=video::Codec::mpeg4;
    auto mp4=video::encode(frames,options);CHECK(mp4);auto read=video::decode(*mp4);
    if(!read)std::cerr<<read.error().message<<'\n';CHECK(read && read->frames.size()==3);save("movie.mp4",*mp4);
    options.container=video::Container::mov;options.codec=video::Codec::mjpeg;
    auto mov=video::encode(frames,options);CHECK(mov);auto mov_read=video::decode(*mov);CHECK(mov_read && mov_read->frames.size()==3);save("movie.mov",*mov);
    options.container=video::Container::avi;options.codec=video::Codec::mjpeg;
    auto avi=video::encode(frames,options);CHECK(avi);auto avi_read=video::decode(*avi);CHECK(avi_read && avi_read->frames.size()==3);save("movie.avi",*avi);
    options={};options.spatial={spatial::Projection::equirectangular,spatial::StereoLayout::side_by_side};
    image.width=64;image.height=16; // each 32x16 eye is a complete 2:1 panorama
    image.rgba.resize(64*16*4);
    for(std::size_t i=0;i<image.rgba.size();i+=4){image.rgba[i]=47;image.rgba[i+1]=81;image.rgba[i+2]=129;image.rgba[i+3]=255;}
    for(auto& f:frames)f.image=image.view();
    auto vr=video::encode(frames,options);if(!vr)std::cerr<<vr.error().message<<'\n';CHECK(vr);
    save("stereo360.mkv",*vr);
    auto spatial_info=video::inspect(*vr);CHECK(spatial_info && spatial_info->spatial.stereo==spatial::StereoLayout::side_by_side && spatial_info->spatial.projection==spatial::Projection::equirectangular);
    options.container=video::Container::mp4;options.codec=video::Codec::mpeg4;
    auto vr_mp4=video::encode(frames,options);CHECK(vr_mp4);auto vr_mp4_info=video::inspect(*vr_mp4);
    save("stereo360.mp4",*vr_mp4);
    if(!vr_mp4_info)std::cerr<<vr_mp4_info.error().message<<'\n';
    CHECK(vr_mp4_info && vr_mp4_info->spatial.stereo==spatial::StereoLayout::side_by_side && vr_mp4_info->spatial.projection==spatial::Projection::equirectangular);
    options.codec=video::Codec::h264;
    auto vr_h264=video::encode(frames,options);if(!vr_h264)std::cerr<<vr_h264.error().message<<'\n';CHECK(vr_h264);
    auto h264_read=video::decode(*vr_h264);CHECK(h264_read && h264_read->frames.size()==3 && h264_read->info.codec=="h264");
    CHECK(h264_read->info.spatial.stereo==spatial::StereoLayout::side_by_side && h264_read->info.spatial.projection==spatial::Projection::equirectangular);
    save("stereo360-h264.mp4",*vr_h264);
    options.container=video::Container::mov;CHECK(!video::encode(frames,options));options.container=video::Container::mp4;
    video::Limits limit;limit.frames=2;CHECK(!video::decode(*bytes,limit));CHECK(!video::encode(frames,options,limit));
    limit={};limit.decoded_bytes=1024;CHECK(!video::decode(*bytes,limit));
    limit={};limit.output_bytes=32;CHECK(!video::encode(frames,options,limit));
    limit={};limit.input_bytes=1;CHECK(!video::inspect(*bytes,limit));
    limit={};limit.image.dimension=1;CHECK(!video::decode(*bytes,limit));
    frames[1].pts=0;CHECK(!video::encode(frames));frames[1].pts=1;
    frames[0].image.row_stride=std::numeric_limits<std::size_t>::max();CHECK(!video::encode(frames));
    CHECK(!video::decode({}));CHECK(!video::decode(std::vector<std::uint8_t>{1,2,3,4}));
    auto truncated=*bytes;truncated.resize(16);CHECK(!video::decode(truncated));
    std::cout<<"video contracts passed: FFV1, MP4, timestamps, VR metadata, limits and invalid inputs\n";
}
