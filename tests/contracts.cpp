#include <iiImageCodec/ImageCodec.h>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace ii::image;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while(false)
int main() {
    // An RGB row has padding; the encoder must not serialize it as pixels.
    const std::vector<std::uint8_t> rgb{255,0,0,0,255,0, 17,18, 0,0,255,255,255,255};
    auto png = encode_png({2,2,PixelFormat::rgb8,8,rgb}); CHECK(png);
    auto info = inspect(*png); CHECK(info && info->width == 2 && info->height == 2 && info->format == Format::png);
    auto image = decode(*png); CHECK(image);
    CHECK((image->rgba == std::vector<std::uint8_t>{255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255}));
    image->rgba[3] = 0;
    auto alpha = encode_png(image->view()); CHECK(alpha);
    auto roundtrip = decode(*alpha); CHECK(roundtrip && roundtrip->rgba == image->rgba);
    CHECK(!encode_jpeg(image->view()));
    std::vector<std::uint8_t> solid(24*24*3,120);
    auto jpg = encode_jpeg({24,24,PixelFormat::rgb8,0,solid},95); CHECK(jpg);
    auto jpegInfo = inspect(*jpg); CHECK(jpegInfo && jpegInfo->format == Format::jpeg);
    auto decoded = decode(*jpg); CHECK(decoded && decoded->width==24 && decoded->height==24);
    CHECK(decoded->rgba[0] >= 118 && decoded->rgba[0] <= 122 && decoded->rgba[3] == 255);
    CHECK(!encode_png({2,2,PixelFormat::rgb8,1,rgb}));
    CHECK(!encode_png({2,2,PixelFormat::rgb8,std::numeric_limits<std::size_t>::max(),rgb}));
    CHECK(!encode_png({0,2,PixelFormat::rgb8,0,rgb}));
    CHECK(!encode_jpeg({24,24,PixelFormat::rgb8,0,solid},101));
    Limits limit; limit.pixel_bytes=15; CHECK(!decode(*png,limit));
    limit={}; limit.dimension=1; CHECK(!inspect(*png,limit));
    limit={}; limit.output_bytes=8; CHECK(!encode_png(image->view(),limit));
    CHECK(!encode_jpeg({24,24,PixelFormat::rgb8,0,solid},90,limit));
    limit={}; limit.input_bytes=2; CHECK(!decode(*png,limit));
    auto corrupt=*png; corrupt.resize(20); CHECK(!decode(corrupt));
    for (std::size_t n=0;n<jpg->size();n+=17) CHECK(!decode(std::span(*jpg).first(n)));
    const std::vector<std::uint8_t> junk{1,2,3}; CHECK(!decode(junk));
    for(auto format:{Format::png,Format::bmp,Format::tga,Format::webp,Format::tiff}) {
        auto bytes=encode(image->view(),format); CHECK(bytes);
        auto details=inspect(*bytes); CHECK(details && details->format==format);
        auto read=decode(*bytes); CHECK(read && read->rgba==image->rgba);
        Limits tiny; tiny.pixel_bytes=1; CHECK(!decode(*bytes,tiny));
        tiny={};tiny.output_bytes=1;CHECK(!encode(image->view(),format,{},tiny));
        auto truncated=*bytes;truncated.resize(7);CHECK(!decode(truncated));
    }
    CHECK(encode(image->view(),Format::jpeg)); CHECK(!encode(image->view(),Format::gif));
    image->metadata.dots_per_meter_x=3780;image->metadata.dots_per_meter_y=3780;
    auto tagged=encode_png(image->view());CHECK(tagged);auto tagged_read=decode(*tagged);CHECK(tagged_read);
    CHECK(tagged_read->metadata.dots_per_meter_x==3780 && tagged_read->metadata.dots_per_meter_y==3780);
    const std::vector<std::uint8_t> gif{71,73,70,56,57,97,1,0,1,0,128,0,0,0,0,0,255,255,255,33,249,4,1,0,0,0,0,44,0,0,0,0,1,0,1,0,0,2,2,68,1,0,59};
    auto gif_read=decode(gif);CHECK(gif_read&&gif_read->width==1&&gif_read->height==1);
    std::vector<std::uint8_t> psd{'8','B','P','S',0,1,0,0,0,0,0,0,0,4,0,0,0,1,0,0,0,1,0,8,0,3,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,17,33,65,255};
    auto psd_read=decode(psd);CHECK(psd_read&&psd_read->width==1&&psd_read->rgba==std::vector<std::uint8_t>({17,33,65,255}));
    // Compare each EXIF transform against the very same decoded JPEG samples,
    // keeping compression loss out of the orientation assertion.
    std::vector<std::uint8_t> pixels{255,0,0,0,255,0,0,0,255,255,255,0,0,255,255,255,0,255};
    auto orientation_jpeg=encode_jpeg({2,3,PixelFormat::rgb8,0,pixels},100);CHECK(orientation_jpeg);
    auto original=decode(*orientation_jpeg);CHECK(original);
    const unsigned orders[8][6]{{0,1,2,3,4,5},{1,0,3,2,5,4},{5,4,3,2,1,0},{4,5,2,3,0,1},
        {0,2,4,1,3,5},{4,2,0,5,3,1},{5,3,1,4,2,0},{1,3,5,0,2,4}};
    for(unsigned orientation=1;orientation<=8;++orientation){
        std::vector<std::uint8_t> exif{255,225,0,34,'E','x','i','f',0,0,'I','I',42,0,8,0,0,0,1,0,18,1,3,0,1,0,0,0,std::uint8_t(orientation),0,0,0,0,0,0,0};
        auto bytes=*orientation_jpeg;bytes.insert(bytes.begin()+2,exif.begin(),exif.end());auto read=decode(bytes);CHECK(read);
        CHECK(read->width==(orientation>=5?3:2)&&read->height==(orientation>=5?2:3)&&read->metadata.orientation==1);
        for(unsigned index=0;index<6;++index)for(unsigned channel=0;channel<4;++channel)CHECK(read->rgba[index*4+channel]==original->rgba[orders[orientation-1][index]*4+channel]);
    }
    // ICC is opaque metadata; these tests assert byte preservation and bounds,
    // independently of a platform's optional color conversion policy.
    image->metadata.icc_profile.resize(512);for(unsigned i=0;i<512;++i)image->metadata.icc_profile[i]=std::uint8_t(i);
    for(auto format:{Format::png,Format::jpeg,Format::webp,Format::tiff}){
        auto bytes=encode(image->view(),format);CHECK(bytes);auto read=decode(*bytes);CHECK(read&&read->metadata.icc_profile==image->metadata.icc_profile);
    }
    image->metadata.icc_profile.resize(1024*1024+1);CHECK(!encode(image->view(),Format::png));
    std::cout << "image contracts passed: formats, alpha, profiles, EXIF 1-8 and bounds\n";
}
