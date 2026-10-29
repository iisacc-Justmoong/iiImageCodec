#include <iiVisualCodec/ImageCodec.h>
#include "Jpeg_p.h"
#include "Codec_p.h"
#include <png.h>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>

namespace ii::visual::image {
namespace {
auto error(ErrorCode code,std::string message) { return std::unexpected(Error{code,std::move(message)}); }
Result<void> extent(std::uint32_t width,std::uint32_t height,Limits limits) {
    if (!width || !height) return error(ErrorCode::invalid_input,"Empty image dimensions");
    if (width>limits.dimension || height>limits.dimension || width>limits.pixel_bytes/4/height)
        return error(ErrorCode::limit_exceeded,"Image dimensions exceed the pixel budget");
    return {};
}
Result<std::size_t> stride(ImageView image,Limits limits) {
    if (auto ok=extent(image.width,image.height,limits); !ok) return std::unexpected(ok.error());
    if (image.format!=PixelFormat::rgb8 && image.format!=PixelFormat::rgba8)
        return error(ErrorCode::invalid_input,"Unknown pixel format");
    const auto row=std::size_t(image.width)*(image.format==PixelFormat::rgb8?3:4);
    const auto stride=image.row_stride?image.row_stride:row;
    if (stride<row || stride>std::numeric_limits<std::size_t>::max()/image.height ||
        image.pixels.size()<stride*(image.height-1)+row)
        return error(ErrorCode::invalid_input,"Pixel span or row stride is invalid");
    return stride;
}
struct Png final {
    png_image image{};
    Png() { image.version=PNG_IMAGE_VERSION; }
    ~Png() { png_image_free(&image); }
};
Format format(std::span<const std::uint8_t> input) {
    return input.size()>=8 && !png_sig_cmp(input.data(),0,8)?Format::png:Format::jpeg;
}
Result<void> input_check(std::span<const std::uint8_t> input,Limits limits) {
    if (input.size()>limits.input_bytes || input.size()>std::numeric_limits<unsigned long>::max())
        return error(ErrorCode::limit_exceeded,"Encoded input exceeds the byte budget");
    if (input.size()<2 || (format(input)==Format::jpeg && (input[0]!=0xff || input[1]!=0xd8)))
        return error(ErrorCode::unsupported_format,"Expected a PNG or JPEG image");
    return {};
}
}
Result<Info> detail::inspect_basic(std::span<const std::uint8_t> input,Limits limits) {
    if (auto ok=input_check(input,limits); !ok) return std::unexpected(ok.error());
    if (format(input)==Format::png) {
        Png png;
        if (!png_image_begin_read_from_memory(&png.image,input.data(),input.size()))
            return error(ErrorCode::codec_failure,png.image.message);
        if (auto ok=extent(png.image.width,png.image.height,limits); !ok) return std::unexpected(ok.error());
        return Info{Format::png,png.image.width,png.image.height};
    }
    auto jpg=ii_jpeg_decode(input.data(),input.size(),limits.dimension,limits.pixel_bytes,1);
    if (jpg.error[0]) return error(jpg.limit?ErrorCode::limit_exceeded:ErrorCode::codec_failure,jpg.error);
    return Info{Format::jpeg,jpg.width,jpg.height};
}
Result<Image> detail::decode_basic(std::span<const std::uint8_t> input,Limits limits) try {
    if (auto ok=input_check(input,limits); !ok) return std::unexpected(ok.error());
    if (format(input)==Format::png) {
        Png png;
        if (!png_image_begin_read_from_memory(&png.image,input.data(),input.size()))
            return error(ErrorCode::codec_failure,png.image.message);
        if (auto ok=extent(png.image.width,png.image.height,limits); !ok) return std::unexpected(ok.error());
        png.image.format=PNG_FORMAT_RGBA;
        Image image{png.image.width,png.image.height,std::vector<std::uint8_t>(std::size_t(png.image.width)*png.image.height*4)};
        if (!png_image_finish_read(&png.image,nullptr,image.rgba.data(),0,nullptr))
            return error(ErrorCode::codec_failure,png.image.message);
        return image;
    }
    auto jpg=ii_jpeg_decode(input.data(),input.size(),limits.dimension,limits.pixel_bytes,0);
    std::unique_ptr<unsigned char,decltype(&std::free)> data(jpg.bytes,std::free);
    if (jpg.error[0]) return error(jpg.limit?ErrorCode::limit_exceeded:ErrorCode::codec_failure,jpg.error);
    return Image{jpg.width,jpg.height,{data.get(),data.get()+jpg.size}};
} catch (const std::bad_alloc&) { return error(ErrorCode::limit_exceeded,"Image allocation failed"); }
Result<std::vector<std::uint8_t>> encode_png(ImageView image,Limits limits) try {
    auto row=stride(image,limits); if (!row) return std::unexpected(row.error());
    if (*row>std::numeric_limits<png_int_32>::max()) return error(ErrorCode::limit_exceeded,"PNG stride is too large");
    Png png; png.image.width=image.width; png.image.height=image.height;
    png.image.format=image.format==PixelFormat::rgb8?PNG_FORMAT_RGB:PNG_FORMAT_RGBA;
    png_alloc_size_t size=0;
    if (!png_image_write_to_memory(&png.image,nullptr,&size,0,image.pixels.data(),static_cast<png_int_32>(*row),nullptr))
        return error(ErrorCode::codec_failure,png.image.message);
    if (size>limits.output_bytes) return error(ErrorCode::limit_exceeded,"PNG output exceeds the byte budget");
    std::vector<std::uint8_t> output(size);
    if (!png_image_write_to_memory(&png.image,output.data(),&size,0,image.pixels.data(),static_cast<png_int_32>(*row),nullptr))
        return error(ErrorCode::codec_failure,png.image.message);
    output.resize(size); return detail::encode_metadata(std::move(output),Format::png,image.metadata,limits);
} catch (const std::bad_alloc&) { return error(ErrorCode::limit_exceeded,"PNG allocation failed"); }
Result<std::vector<std::uint8_t>> encode_jpeg(ImageView image,int quality,Limits limits) try {
    auto row=stride(image,limits); if (!row) return std::unexpected(row.error());
    if (image.format!=PixelFormat::rgb8 || quality<1 || quality>100)
        return error(ErrorCode::invalid_input,"JPEG requires RGB pixels and quality in 1..100");
    auto jpg=ii_jpeg_encode(image.pixels.data(),image.width,image.height,*row,quality,limits.output_bytes);
    std::unique_ptr<unsigned char,decltype(&std::free)> data(jpg.bytes,std::free);
    if (jpg.error[0]) return error(jpg.limit?ErrorCode::limit_exceeded:ErrorCode::codec_failure,jpg.error);
    return detail::encode_metadata(std::vector<std::uint8_t>(data.get(),data.get()+jpg.size),Format::jpeg,image.metadata,limits);
} catch (const std::bad_alloc&) { return error(ErrorCode::limit_exceeded,"JPEG allocation failed"); }
}
