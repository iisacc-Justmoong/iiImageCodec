#include <iiVisualCodec/OpenCV.h>
#include <iiVisualCodec/ImageCodec.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iterator>
int main(int argc,char **argv) {
    using namespace ii::visual;
    if(argc!=3) {std::cerr<<"usage: vision_pipeline input.png output.png\n";return 2;}
    std::error_code error;
    const auto size=std::filesystem::file_size(argv[1],error);
    if(error || size>64*1024*1024) {std::cerr<<"Input must be an available image below 64 MiB\n";return 2;}
    std::ifstream in(argv[1],std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
    auto decoded=image::decode(bytes);if(!decoded){std::cerr<<decoded.error().message;return 1;}
    auto gray=opencv::grayscale(decoded->view());if(!gray){std::cerr<<gray.error().message;return 1;}
    auto encoded=image::encode_png(gray->view());if(!encoded){std::cerr<<encoded.error().message;return 1;}
    std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<const char*>(encoded->data()),encoded->size());
    return out?0:1;
}
