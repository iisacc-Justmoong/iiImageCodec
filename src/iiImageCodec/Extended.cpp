#include "Codec_p.h"
#include <webp/decode.h>
#include <webp/encode.h>
#include <tiffio.h>
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstring>
#include <limits>
#include <memory>
#define STBI_NO_STDIO
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#define STBI_ONLY_PSD
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace ii::image {
namespace {
auto fail(ErrorCode code,std::string message){return std::unexpected(Error{code,std::move(message)});}
Format detect(std::span<const std::uint8_t> p){
    if(p.size()>=8 && std::memcmp(p.data(),"\x89PNG\r\n\x1a\n",8)==0)return Format::png;
    if(p.size()>=2 && p[0]==255 && p[1]==216)return Format::jpeg;
    if(p.size()>=2 && p[0]=='B' && p[1]=='M')return Format::bmp;
    if(p.size()>=6 && (std::memcmp(p.data(),"GIF87a",6)==0 || std::memcmp(p.data(),"GIF89a",6)==0))return Format::gif;
    if(p.size()>=4 && std::memcmp(p.data(),"8BPS",4)==0)return Format::psd;
    if(p.size()>=12 && std::memcmp(p.data(),"RIFF",4)==0 && std::memcmp(p.data()+8,"WEBP",4)==0)return Format::webp;
    if(p.size()>=4 && ((p[0]=='I'&&p[1]=='I'&&(p[2]==42||p[2]==43)&&p[3]==0)||
        (p[0]=='M'&&p[1]=='M'&&p[2]==0&&(p[3]==42||p[3]==43))))return Format::tiff;
    if(p.size()>=18 && (p[2]==2||p[2]==3||p[2]==10||p[2]==11))return Format::tga;
    return Format::unknown;
}
Result<void> bounded(std::uint32_t w,std::uint32_t h,Limits l){
    if(!w||!h)return fail(ErrorCode::invalid_input,"Empty image dimensions");
    if(w>l.dimension||h>l.dimension||w>l.pixel_bytes/4/h)return fail(ErrorCode::limit_exceeded,"Image dimensions exceed pixel budget");
    return {};
}
struct Buffer{
    std::span<const std::uint8_t> input;
    std::vector<std::uint8_t> output;
    std::uint64_t position=0;
    std::size_t maximum=0;
    bool writing=false,failed=false;
};
tmsize_t readTiff(thandle_t raw,void* data,tmsize_t size){
    auto& b=*static_cast<Buffer*>(raw);if(size<0||b.position>b.input.size())return 0;
    const auto count=std::min<std::size_t>(size,b.input.size()-b.position);
    if(count)std::memcpy(data,b.input.data()+b.position,count);b.position+=count;return tmsize_t(count);
}
tmsize_t writeTiff(thandle_t raw,void* data,tmsize_t size){
    auto& b=*static_cast<Buffer*>(raw);
    if(!b.writing||size<0||b.position>b.maximum||std::uint64_t(size)>b.maximum-b.position){b.failed=true;return 0;}
    try{if(b.position+size>b.output.size())b.output.resize(b.position+size);if(size)std::memcpy(b.output.data()+b.position,data,size);b.position+=size;return size;}
    catch(...){b.failed=true;return 0;}
}
toff_t seekTiff(thandle_t raw,toff_t offset,int origin){
    auto& b=*static_cast<Buffer*>(raw);const auto maximum=b.writing?b.maximum:b.input.size();
    const auto base=origin==SEEK_SET?0:origin==SEEK_CUR?b.position:origin==SEEK_END?(b.writing?b.output.size():b.input.size()):UINT64_MAX;
    if(base>maximum)return toff_t(-1);
    const auto delta=static_cast<std::int64_t>(offset);
    if((delta<0 && std::uint64_t(-(delta+1))+1>base)||(delta>=0&&std::uint64_t(delta)>maximum-base))return toff_t(-1);
    b.position=delta<0?base-(std::uint64_t(-(delta+1))+1):base+std::uint64_t(delta);return b.position;
}
int closeTiff(thandle_t){return 0;}
toff_t sizeTiff(thandle_t raw){auto& b=*static_cast<Buffer*>(raw);return b.writing?b.output.size():b.input.size();}
int mapTiff(thandle_t,void**,toff_t*){return 0;}
void unmapTiff(thandle_t,void*,toff_t){}
int quietTiff(TIFF*,void*,const char*,const char*,va_list){return 1;}
auto openTiff(Buffer& b,const char* mode,Limits limits){
    std::unique_ptr<TIFFOpenOptions,decltype(&TIFFOpenOptionsFree)> options(TIFFOpenOptionsAlloc(),TIFFOpenOptionsFree);
    if(options){TIFFOpenOptionsSetMaxSingleMemAlloc(options.get(),tmsize_t(std::min<std::size_t>(limits.pixel_bytes,std::numeric_limits<tmsize_t>::max())));
        TIFFOpenOptionsSetErrorHandlerExtR(options.get(),quietTiff,nullptr);TIFFOpenOptionsSetWarningHandlerExtR(options.get(),quietTiff,nullptr);}
    return std::unique_ptr<TIFF,decltype(&TIFFClose)>(TIFFClientOpenExt("memory",mode,&b,readTiff,writeTiff,seekTiff,closeTiff,sizeTiff,mapTiff,unmapTiff,options.get()),TIFFClose);
}
Result<Info> tiffInfo(TIFF* t,Limits limits){
    std::uint32_t w=0,h=0;
    if(!t||!TIFFGetField(t,TIFFTAG_IMAGEWIDTH,&w)||!TIFFGetField(t,TIFFTAG_IMAGELENGTH,&h))return fail(ErrorCode::codec_failure,"Invalid TIFF directory");
    if(auto result=bounded(w,h,limits);!result)return std::unexpected(result.error());
    return Info{Format::tiff,w,h};
}
void tiffMetadata(TIFF* t,Metadata& m){
    std::uint32_t count=0;void* profile=nullptr;
    if(TIFFGetField(t,TIFFTAG_ICCPROFILE,&count,&profile)&&count<=1024*1024&&profile)
        m.icc_profile.assign(static_cast<const std::uint8_t*>(profile),static_cast<const std::uint8_t*>(profile)+count);
    std::uint16_t orientation=1,unit=RESUNIT_NONE;float x=0,y=0;
    TIFFGetFieldDefaulted(t,TIFFTAG_ORIENTATION,&orientation);m.orientation=orientation>=1&&orientation<=8?orientation:1;
    TIFFGetFieldDefaulted(t,TIFFTAG_RESOLUTIONUNIT,&unit);
    TIFFGetField(t,TIFFTAG_XRESOLUTION,&x);TIFFGetField(t,TIFFTAG_YRESOLUTION,&y);
    const double scale=unit==RESUNIT_INCH?1/.0254:unit==RESUNIT_CENTIMETER?100:0;
    if(x>0&&std::isfinite(x)&&x*scale<=UINT32_MAX)m.dots_per_meter_x=std::uint32_t(std::lround(x*scale));
    if(y>0&&std::isfinite(y)&&y*scale<=UINT32_MAX)m.dots_per_meter_y=std::uint32_t(std::lround(y*scale));
}
void unassociate(std::uint8_t* p){if(p[3]&&p[3]!=255)for(int c=0;c<3;++c)p[c]=std::uint8_t(std::min(255u,(unsigned(p[c])*255+p[3]/2)/p[3]));}
Result<Image> decodeTiff(std::span<const std::uint8_t> input,Limits limits){
    Buffer buffer{.input=input};auto t=openTiff(buffer,"r",limits);auto info=tiffInfo(t.get(),limits);if(!info)return std::unexpected(info.error());
    Image image{info->width,info->height,std::vector<std::uint8_t>(std::size_t(info->width)*info->height*4)};tiffMetadata(t.get(),image.metadata);
    std::uint16_t bits=0,samples=0,photo=0,planar=0,extra_count=0;std::uint16_t* extras=nullptr;
    TIFFGetFieldDefaulted(t.get(),TIFFTAG_BITSPERSAMPLE,&bits);TIFFGetFieldDefaulted(t.get(),TIFFTAG_SAMPLESPERPIXEL,&samples);
    TIFFGetFieldDefaulted(t.get(),TIFFTAG_PHOTOMETRIC,&photo);TIFFGetFieldDefaulted(t.get(),TIFFTAG_PLANARCONFIG,&planar);
    TIFFGetField(t.get(),TIFFTAG_EXTRASAMPLES,&extra_count,&extras);
    if(bits==8&&(samples==3||samples==4)&&photo==PHOTOMETRIC_RGB&&planar==PLANARCONFIG_CONTIG&&!TIFFIsTiled(t.get())){
        const auto row_size=TIFFScanlineSize64(t.get());
        if(row_size<std::uint64_t(image.width)*samples||row_size>limits.pixel_bytes)return fail(ErrorCode::limit_exceeded,"Invalid TIFF row size");
        std::vector<std::uint8_t> row(row_size);
        for(std::uint32_t y=0;y<image.height;++y){
            if(TIFFReadScanline(t.get(),row.data(),y,0)<0)return fail(ErrorCode::codec_failure,"Cannot decode TIFF row");
            for(std::uint32_t x=0;x<image.width;++x){auto* p=image.rgba.data()+(std::size_t(y)*image.width+x)*4;
                std::copy_n(row.data()+std::size_t(x)*samples,3,p);p[3]=samples==4?row[std::size_t(x)*samples+3]:255;
                if(samples==4&&extra_count&&extras[0]==EXTRASAMPLE_ASSOCALPHA)unassociate(p);}
        }
    }else{
        std::vector<std::uint32_t> pixels(std::size_t(image.width)*image.height);
        if(!TIFFReadRGBAImageOriented(t.get(),image.width,image.height,pixels.data(),ORIENTATION_TOPLEFT,1))return fail(ErrorCode::codec_failure,"Cannot decode TIFF pixels");
        for(std::size_t i=0;i<pixels.size();++i){auto* p=image.rgba.data()+i*4;const auto value=pixels[i];
            p[0]=TIFFGetR(value);p[1]=TIFFGetG(value);p[2]=TIFFGetB(value);p[3]=TIFFGetA(value);unassociate(p);}
        image.metadata.orientation=1;
    }
    return image;
}
struct Output{std::vector<std::uint8_t> bytes;std::size_t limit;bool failed=false;};
void appendOutput(void* raw,void* data,int count){
    auto& o=*static_cast<Output*>(raw);if(count<0||std::size_t(count)>o.limit-o.bytes.size()){o.failed=true;return;}
    try{const auto* p=static_cast<const std::uint8_t*>(data);o.bytes.insert(o.bytes.end(),p,p+count);}catch(...){o.failed=true;}
}
int webpOutput(const std::uint8_t* data,std::size_t count,const WebPPicture* picture){
    auto& o=*static_cast<Output*>(picture->custom_ptr);if(count>o.limit-o.bytes.size()){o.failed=true;return 0;}
    try{o.bytes.insert(o.bytes.end(),data,data+count);return 1;}catch(...){o.failed=true;return 0;}
}
}
Result<Info> inspect(std::span<const std::uint8_t> input,Limits limits) try{
    if(input.size()>limits.input_bytes||input.size()>std::size_t(INT_MAX))return fail(ErrorCode::limit_exceeded,"Encoded input exceeds byte budget");
    const auto format=detect(input);
    if(format==Format::png||format==Format::jpeg)return detail::inspect_basic(input,limits);
    if(format==Format::tiff){Buffer buffer{.input=input};auto t=openTiff(buffer,"r",limits);return tiffInfo(t.get(),limits);}
    int w=0,h=0,channels=0;
    if(format==Format::webp){if(!WebPGetInfo(input.data(),input.size(),&w,&h))return fail(ErrorCode::codec_failure,"Invalid WebP image");}
    else if(format==Format::unknown)return fail(ErrorCode::unsupported_format,"Unsupported bitmap format");
    else if(!stbi_info_from_memory(input.data(),int(input.size()),&w,&h,&channels))return fail(ErrorCode::codec_failure,"Invalid bitmap header");
    if(auto result=bounded(w,h,limits);!result)return std::unexpected(result.error());return Info{format,std::uint32_t(w),std::uint32_t(h)};
}catch(const std::bad_alloc&){return fail(ErrorCode::limit_exceeded,"Image metadata allocation failed");}
Result<Image> decode(std::span<const std::uint8_t> input,Limits limits) try{
    auto info=inspect(input,limits);if(!info)return std::unexpected(info.error());
    if(info->format==Format::png||info->format==Format::jpeg){auto image=detail::decode_basic(input,limits);if(!image)return image;return detail::decode_metadata(input,std::move(*image),info->format);}
    if(info->format==Format::tiff){auto image=decodeTiff(input,limits);if(!image)return image;return detail::decode_metadata(input,std::move(*image),Format::tiff);}
    Image image{info->width,info->height,std::vector<std::uint8_t>(std::size_t(info->width)*info->height*4)};
    if(info->format==Format::webp){if(!WebPDecodeRGBAInto(input.data(),input.size(),image.rgba.data(),image.rgba.size(),int(image.width*4)))return fail(ErrorCode::codec_failure,"Cannot decode WebP pixels");}
    else{int w=0,h=0,channels=0;std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(input.data(),int(input.size()),&w,&h,&channels,4),stbi_image_free);
        if(!pixels||w!=int(image.width)||h!=int(image.height))return fail(ErrorCode::codec_failure,"Cannot decode bitmap pixels");std::copy_n(pixels.get(),image.rgba.size(),image.rgba.data());}
    return detail::decode_metadata(input,std::move(image),info->format);
}catch(const std::bad_alloc&){return fail(ErrorCode::limit_exceeded,"Image allocation failed");}
Result<std::vector<std::uint8_t>> encode(ImageView image,Format format,EncodeOptions options,Limits limits) try{
    if(format==Format::png)return encode_png(image,limits);
    if(auto result=bounded(image.width,image.height,limits);!result)return std::unexpected(result.error());
    if(image.format!=PixelFormat::rgb8&&image.format!=PixelFormat::rgba8)return fail(ErrorCode::invalid_input,"Unknown pixel format");
    const int channels=image.format==PixelFormat::rgba8?4:3;const auto row=image.row_stride?image.row_stride:std::size_t(image.width)*channels;
    if(row<std::size_t(image.width)*channels||row>SIZE_MAX/image.height||image.pixels.size()<row*(image.height-1)+std::size_t(image.width)*channels||options.quality<1||options.quality>100)
        return fail(ErrorCode::invalid_input,"Invalid image span, stride or quality");
    std::vector<std::uint8_t> rgba(std::size_t(image.width)*image.height*4);
    for(std::uint32_t y=0;y<image.height;++y)for(std::uint32_t x=0;x<image.width;++x){auto* p=rgba.data()+(std::size_t(y)*image.width+x)*4;const auto* s=image.pixels.data()+y*row+x*channels;std::copy_n(s,3,p);p[3]=channels==4?s[3]:255;}
    if(format==Format::jpeg){std::vector<std::uint8_t> rgb(std::size_t(image.width)*image.height*3);
        for(std::size_t i=0;i<rgba.size()/4;++i)for(unsigned c=0;c<3;++c){const unsigned alpha=rgba[i*4+3],matte=(options.matte_rgb>>(16-c*8))&255;rgb[i*3+c]=std::uint8_t((unsigned(rgba[i*4+c])*alpha+matte*(255-alpha)+127)/255);}
        return encode_jpeg({image.width,image.height,PixelFormat::rgb8,0,rgb,image.metadata},options.quality,limits);}
    Output output{{},limits.output_bytes};
    if(format==Format::bmp||format==Format::tga){
        const auto ok=format==Format::bmp?stbi_write_bmp_to_func(appendOutput,&output,int(image.width),int(image.height),4,rgba.data()):stbi_write_tga_to_func(appendOutput,&output,int(image.width),int(image.height),4,rgba.data());
        if(!ok||output.failed)return fail(output.failed?ErrorCode::limit_exceeded:ErrorCode::codec_failure,"Bitmap output failed");return output.bytes;
    }
    if(format==Format::webp){
        WebPConfig config{};WebPPicture picture{};if(!WebPConfigInit(&config)||!WebPPictureInit(&picture))return fail(ErrorCode::codec_failure,"WebP version mismatch");
        config.lossless=1;config.exact=1;config.quality=options.quality;config.method=4;
        picture.use_argb=1;picture.width=int(image.width);picture.height=int(image.height);picture.writer=webpOutput;picture.custom_ptr=&output;
        const auto imported=WebPPictureImportRGBA(&picture,rgba.data(),int(image.width*4));const auto ok=imported&&WebPEncode(&config,&picture);WebPPictureFree(&picture);
        if(!ok||output.failed)return fail(output.failed?ErrorCode::limit_exceeded:ErrorCode::codec_failure,"WebP output failed");return detail::encode_metadata(std::move(output.bytes),Format::webp,image.metadata,limits);
    }
    if(format==Format::tiff){
        Buffer buffer{.maximum=limits.output_bytes,.writing=true};auto t=openTiff(buffer,"w",limits);if(!t)return fail(ErrorCode::codec_failure,"Cannot create TIFF output");
        std::uint16_t alpha=EXTRASAMPLE_UNASSALPHA;
        TIFFSetField(t.get(),TIFFTAG_IMAGEWIDTH,image.width);TIFFSetField(t.get(),TIFFTAG_IMAGELENGTH,image.height);
        TIFFSetField(t.get(),TIFFTAG_BITSPERSAMPLE,8);TIFFSetField(t.get(),TIFFTAG_SAMPLESPERPIXEL,4);TIFFSetField(t.get(),TIFFTAG_PHOTOMETRIC,PHOTOMETRIC_RGB);
        TIFFSetField(t.get(),TIFFTAG_PLANARCONFIG,PLANARCONFIG_CONTIG);TIFFSetField(t.get(),TIFFTAG_ORIENTATION,ORIENTATION_TOPLEFT);TIFFSetField(t.get(),TIFFTAG_EXTRASAMPLES,1,&alpha);
        TIFFSetField(t.get(),TIFFTAG_COMPRESSION,COMPRESSION_LZW);TIFFSetField(t.get(),TIFFTAG_ROWSPERSTRIP,TIFFDefaultStripSize(t.get(),0));
        if(image.metadata){const auto& m=*image.metadata;if(m.icc_profile.size()>1024*1024)return fail(ErrorCode::limit_exceeded,"ICC profile exceeds metadata budget");
            if(!m.icc_profile.empty())TIFFSetField(t.get(),TIFFTAG_ICCPROFILE,std::uint32_t(m.icc_profile.size()),m.icc_profile.data());
            if(m.dots_per_meter_x&&m.dots_per_meter_y){TIFFSetField(t.get(),TIFFTAG_RESOLUTIONUNIT,RESUNIT_CENTIMETER);TIFFSetField(t.get(),TIFFTAG_XRESOLUTION,double(m.dots_per_meter_x)/100);TIFFSetField(t.get(),TIFFTAG_YRESOLUTION,double(m.dots_per_meter_y)/100);}}
        for(std::uint32_t y=0;y<image.height;++y)if(TIFFWriteScanline(t.get(),rgba.data()+std::size_t(y)*image.width*4,y,0)<0)return fail(buffer.failed?ErrorCode::limit_exceeded:ErrorCode::codec_failure,"TIFF row output failed");
        t.reset();if(buffer.failed)return fail(ErrorCode::limit_exceeded,"TIFF output exceeds byte budget");return buffer.output;
    }
    return fail(ErrorCode::unsupported_format,"This format is read-only or unsupported");
}catch(const std::bad_alloc&){return fail(ErrorCode::limit_exceeded,"Image encoder allocation failed");}
}
