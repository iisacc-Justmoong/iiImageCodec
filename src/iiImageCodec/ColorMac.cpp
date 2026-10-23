#include "Color_p.h"
#include <CoreGraphics/CoreGraphics.h>
#include <algorithm>

namespace ii::image {
Result<Image> to_srgb(Image image,Limits limits)try{
    if(!detail::valid_color_image(image,limits))return detail::color_error("ICC conversion image or profile exceeds its bounds");if(image.metadata.icc_profile.empty())return image;
    auto data=CFDataCreate(kCFAllocatorDefault,image.metadata.icc_profile.data(),CFIndex(image.metadata.icc_profile.size()));auto source=CGColorSpaceCreateWithICCData(data);CFRelease(data);if(!source)return detail::color_error("ICC profile is invalid");
    struct Space final{CGColorSpaceRef p;~Space(){if(p)CGColorSpaceRelease(p);}}source_owner{source};if(CGColorSpaceGetModel(source)!=kCGColorSpaceModelRGB)return detail::color_error("ICC conversion requires an RGB source profile");auto target=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);Space target_owner{target};if(!target)return detail::color_error("Cannot create native sRGB color space");
    auto provider=CGDataProviderCreateWithData(nullptr,image.rgba.data(),image.rgba.size(),nullptr);struct Provider final{CGDataProviderRef p;~Provider(){if(p)CGDataProviderRelease(p);}}provider_owner{provider};if(!provider)return detail::color_error("Cannot create bounded ICC input");
    auto bitmap=CGImageCreate(image.width,image.height,8,32,image.width*4,source,CGBitmapInfo(kCGImageAlphaLast)|kCGBitmapByteOrder32Big,provider,nullptr,false,kCGRenderingIntentRelativeColorimetric);struct Bitmap final{CGImageRef p;~Bitmap(){if(p)CGImageRelease(p);}}bitmap_owner{bitmap};if(!bitmap)return detail::color_error("Cannot interpret the ICC source pixels");
    std::vector<std::uint8_t> converted(image.rgba.size());auto context=CGBitmapContextCreate(converted.data(),image.width,image.height,8,image.width*4,target,CGBitmapInfo(kCGImageAlphaPremultipliedLast)|kCGBitmapByteOrder32Big);if(!context)return detail::color_error("Cannot allocate native color conversion surface");
    CGContextSetBlendMode(context,kCGBlendModeCopy);CGContextSetInterpolationQuality(context,kCGInterpolationNone);CGContextDrawImage(context,CGRectMake(0,0,image.width,image.height),bitmap);CGContextRelease(context);
    for(std::size_t i=0;i<converted.size();i+=4){const auto alpha=converted[i+3];if(alpha)for(unsigned channel=0;channel<3;++channel)converted[i+channel]=std::uint8_t(std::min(255u,(unsigned(converted[i+channel])*255+alpha/2)/alpha));else converted[i]=converted[i+1]=converted[i+2]=0;}
    auto profile=CGColorSpaceCopyICCData(target);if(!profile)return detail::color_error("Cannot export converted sRGB profile");image.metadata.icc_profile.assign(CFDataGetBytePtr(profile),CFDataGetBytePtr(profile)+CFDataGetLength(profile));CFRelease(profile);image.rgba=std::move(converted);return image;
}catch(const std::bad_alloc&){return detail::color_error("ICC conversion allocation failed");}
}
