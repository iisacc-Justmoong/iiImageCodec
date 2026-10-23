#define NOMINMAX
#include "Color_p.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
namespace ii::image {
Result<Image> to_srgb(Image image,Limits limits)try{if(!detail::valid_color_image(image,limits)||image.rgba.size()>UINT_MAX)return detail::color_error("ICC conversion image or profile exceeds its bounds");if(image.metadata.icc_profile.empty())return image;
    const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);struct Apartment final{bool release;~Apartment(){if(release)CoUninitialize();}}apartment{SUCCEEDED(initialized)};if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE)return detail::color_error("Cannot initialize native color management");using Microsoft::WRL::ComPtr;ComPtr<IWICImagingFactory> factory;if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return detail::color_error("Cannot create native WIC factory");ComPtr<IWICBitmap> bitmap;ComPtr<IWICColorContext> source,target;ComPtr<IWICColorTransform> transform;
    if(FAILED(factory->CreateBitmapFromMemory(image.width,image.height,GUID_WICPixelFormat32bppRGBA,image.width*4,UINT(image.rgba.size()),image.rgba.data(),&bitmap))||FAILED(factory->CreateColorContext(&source))||FAILED(factory->CreateColorContext(&target))||FAILED(source->InitializeFromMemory(image.metadata.icc_profile.data(),UINT(image.metadata.icc_profile.size())))||FAILED(target->InitializeFromExifColorSpace(1))||FAILED(factory->CreateColorTransformer(&transform))||FAILED(transform->Initialize(bitmap.Get(),source.Get(),target.Get(),GUID_WICPixelFormat32bppRGBA)))return detail::color_error("ICC profile or WIC transform is unsupported");
    std::vector<std::uint8_t> converted(image.rgba.size());if(FAILED(transform->CopyPixels(nullptr,image.width*4,UINT(converted.size()),converted.data())))return detail::color_error("Native WIC color conversion failed");image.rgba=std::move(converted);
    // Empty profile is the codec contract's standard sRGB default. WIC Exif
    // color context 1 is sRGB even when it has no materialized ICC byte blob.
    image.metadata.icc_profile.clear();return image;
}catch(const std::bad_alloc&){return detail::color_error("ICC conversion allocation failed");}
}
