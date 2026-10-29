#include <iiImageCodec/ImageCodec.h>
#include <type_traits>
int main() {
    static_assert(std::is_same_v<ii::image::Image,ii::visual::image::Image>);
    ii::image::Image image{1,1,{1,2,3,255}};
    auto bytes=ii::image::encode_png(image.view());if(!bytes)return 1;
    auto read=ii::image::decode(*bytes);
    return !read || read->rgba!=image.rgba;
}
