#pragma once
#include "ImageCodec.h"
namespace ii::image::detail {
inline bool valid_color_image(const Image& image,Limits limits){return limits.pixel_bytes<=256*1024*1024&&limits.dimension<=32768&&image.width&&image.height&&image.width<=limits.dimension&&image.height<=limits.dimension&&std::uint64_t(image.width)*image.height<=limits.pixel_bytes/4&&image.rgba.size()==std::uint64_t(image.width)*image.height*4&&image.metadata.icc_profile.size()<=1024*1024;}
inline std::unexpected<Error> color_error(std::string text){return std::unexpected(Error{ErrorCode::codec_failure,std::move(text)});}
}
