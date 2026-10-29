#include "Color_p.h"
namespace ii::visual::image {
Result<Image> to_srgb(Image image,Limits limits){if(!detail::valid_color_image(image,limits))return detail::color_error("Invalid ICC conversion image");if(image.metadata.icc_profile.empty())return image;return std::unexpected(Error{ErrorCode::unsupported_format,"ICC conversion is unavailable on this platform"});}
}
