#include <iiVisualCodec/VideoCodec.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc,char** argv){
    if(argc<2)return 1;
    for(int i=1;i<argc;++i){
        std::ifstream input(argv[i],std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        auto video=ii::visual::video::decode(bytes);
        if(!video){std::cerr<<argv[i]<<": "<<video.error().message<<'\n';return 2;}
        if(video->info.width!=16 || video->info.height!=16 || video->frames.size()!=3)return 3;
        for(const auto& frame:video->frames){
            if(frame.image.width!=16 || frame.image.height!=16 || frame.image.rgba.size()!=16*16*4)return 4;
            if(frame.image.rgba[0]<230 || frame.image.rgba[1]>30 || frame.image.rgba[2]>30 || frame.image.rgba[3]!=255)return 5;
        }
        std::cout<<std::filesystem::path(argv[i]).filename()<<": "<<video->info.codec<<", 3 frames\n";
    }
}
