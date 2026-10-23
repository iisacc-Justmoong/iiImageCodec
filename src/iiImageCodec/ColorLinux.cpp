#include "Color_p.h"
#include <lcms2.h>
namespace ii::image {
Result<Image> to_srgb(Image image,Limits limits)try{if(!detail::valid_color_image(image,limits))return detail::color_error("ICC conversion image or profile exceeds its bounds");if(image.metadata.icc_profile.empty())return image;
    struct Profile final{cmsHPROFILE value;~Profile(){if(value)cmsCloseProfile(value);}}source{cmsOpenProfileFromMem(image.metadata.icc_profile.data(),cmsUInt32Number(image.metadata.icc_profile.size()))},target{cmsCreate_sRGBProfile()};if(!source.value||!target.value||cmsGetColorSpace(source.value)!=cmsSigRgbData)return detail::color_error("ICC conversion requires valid RGB profiles");
    struct Transform final{cmsHTRANSFORM value;~Transform(){if(value)cmsDeleteTransform(value);}}transform{cmsCreateTransform(source.value,TYPE_RGBA_8,target.value,TYPE_RGBA_8,INTENT_RELATIVE_COLORIMETRIC,cmsFLAGS_COPY_ALPHA|cmsFLAGS_NOCACHE)};if(!transform.value)return detail::color_error("Cannot create native ICC transform");cmsDoTransform(transform.value,image.rgba.data(),image.rgba.data(),image.width*image.height);cmsUInt32Number size=0;if(!cmsSaveProfileToMem(target.value,nullptr,&size)||size>1024*1024)return detail::color_error("Cannot export converted sRGB profile");image.metadata.icc_profile.resize(size);if(!cmsSaveProfileToMem(target.value,image.metadata.icc_profile.data(),&size))return detail::color_error("Cannot export converted sRGB profile");return image;
}catch(const std::bad_alloc&){return detail::color_error("ICC conversion allocation failed");}
}
