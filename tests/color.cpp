#include <iiImageCodec/ImageCodec.h>
#include <iostream>
#include <cmath>
#include <stdexcept>
#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#endif
int main(){try{ii::image::Image source{2,2,{200,100,50,255,20,150,80,255,30,60,180,128,50,80,120,255}};auto unchanged=ii::image::to_srgb(source);if(!unchanged||unchanged->rgba!=source.rgba)throw std::runtime_error("Implicit sRGB pixels changed");
    auto invalid=source;invalid.metadata.icc_profile={1,2,3};if(ii::image::to_srgb(std::move(invalid)))throw std::runtime_error("Invalid ICC profile accepted");
#if defined(__APPLE__)
    auto p3=CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3);auto data=CGColorSpaceCopyICCData(p3);source.metadata.icc_profile.assign(CFDataGetBytePtr(data),CFDataGetBytePtr(data)+CFDataGetLength(data));CFRelease(data);CGColorSpaceRelease(p3);auto converted=ii::image::to_srgb(source);if(!converted)throw std::runtime_error(converted.error().message);if(converted->width!=2||converted->height!=2)throw std::runtime_error("ICC conversion changed dimensions");
    if(converted->rgba[0]<=source.rgba[0]||converted->rgba[1]>=source.rgba[1]||converted->rgba[2]>=source.rgba[2])throw std::runtime_error("P3 to sRGB conversion did not apply the primary matrix");for(std::size_t i=3;i<source.rgba.size();i+=4)if(converted->rgba[i]!=source.rgba[i])throw std::runtime_error("ICC conversion changed alpha or row order");if(converted->metadata.icc_profile.empty())throw std::runtime_error("sRGB output profile missing");
#endif
    std::cout<<"NATIVE_COLOR_OK rgb=true alpha=true bounds=true profile=true\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
