#include "Codec_p.h"
#include <zlib.h>
#include <webp/decode.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace ii::visual::image::detail {
namespace {
constexpr std::size_t ProfileLimit=1024*1024;
auto fail(ErrorCode code,std::string text){return std::unexpected(Error{code,std::move(text)});}
std::uint32_t be32(const std::uint8_t* p){return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];}
std::uint32_t le32(const std::uint8_t* p){return (std::uint32_t(p[3])<<24)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[1])<<8)|p[0];}
void put32(std::vector<std::uint8_t>& p,std::uint32_t n){for(int i=3;i>=0;--i)p.push_back(std::uint8_t(n>>(i*8)));}
void little32(std::vector<std::uint8_t>& p,std::uint32_t n){for(int i=0;i<4;++i)p.push_back(std::uint8_t(n>>(i*8)));}
unsigned exifOrientation(std::span<const std::uint8_t> bytes){
    if(bytes.size()<8)return 1;
    const bool little=bytes[0]=='I'&&bytes[1]=='I';if(!little&&!(bytes[0]=='M'&&bytes[1]=='M'))return 1;
    auto u16=[&](std::size_t n)->unsigned{return little?unsigned(bytes[n])|(unsigned(bytes[n+1])<<8):(unsigned(bytes[n])<<8)|bytes[n+1];};
    auto u32=[&](std::size_t n)->std::uint32_t{return little?le32(bytes.data()+n):be32(bytes.data()+n);};
    if(u16(2)!=42)return 1;const auto offset=std::size_t(u32(4));
    if(offset>bytes.size()||bytes.size()-offset<2)return 1;const auto count=u16(offset);
    if(count>1024||count>(bytes.size()-offset-2)/12)return 1;
    for(unsigned i=0;i<count;++i){const auto at=offset+2+i*12;
        if(u16(at)==0x112&&u16(at+2)==3&&u32(at+4)==1){const auto value=u16(at+8);return value>=1&&value<=8?value:1;}}
    return 1;
}
void pngChunk(std::vector<std::uint8_t>& output,const char* name,std::span<const std::uint8_t> data){
    put32(output,std::uint32_t(data.size()));const auto at=output.size();output.insert(output.end(),name,name+4);output.insert(output.end(),data.begin(),data.end());
    put32(output,std::uint32_t(crc32(0,output.data()+at,uInt(data.size()+4))));
}
Result<void> pngMetadata(std::span<const std::uint8_t> input,Metadata& metadata){
    for(std::size_t offset=8;offset<=input.size()&&input.size()-offset>=12;){
        const auto size=std::size_t(be32(input.data()+offset));if(size>input.size()-offset-12)return fail(ErrorCode::codec_failure,"Invalid PNG metadata length");
        const auto* name=input.data()+offset+4;const auto data=input.subspan(offset+8,size);
        if(std::memcmp(name,"pHYs",4)==0&&size==9&&data[8]==1){metadata.dots_per_meter_x=be32(data.data());metadata.dots_per_meter_y=be32(data.data()+4);}
        if(std::memcmp(name,"eXIf",4)==0)metadata.orientation=exifOrientation(data);
        if(std::memcmp(name,"iCCP",4)==0){
            auto end=std::find(data.begin(),data.end(),0);const auto length=std::size_t(end-data.begin());
            if(end==data.end()||length==0||length>79||data.size()-length<3||data[length+1]!=0)return fail(ErrorCode::codec_failure,"Invalid PNG profile header");
            if(be32(input.data()+offset+8+size)!=crc32(0,name,uInt(size+4)))return fail(ErrorCode::codec_failure,"Invalid PNG profile checksum");
            metadata.icc_profile.resize(ProfileLimit);uLongf written=ProfileLimit;
            const auto rc=uncompress(metadata.icc_profile.data(),&written,data.data()+length+2,uLong(data.size()-length-2));
            if(rc!=Z_OK)return fail(rc==Z_BUF_ERROR?ErrorCode::limit_exceeded:ErrorCode::codec_failure,"Invalid or oversized PNG profile");metadata.icc_profile.resize(written);
        }
        offset+=size+12;
    }
    return {};
}
Result<void> jpegMetadata(std::span<const std::uint8_t> input,Metadata& metadata){
    std::array<std::vector<std::uint8_t>,256> parts;unsigned total=0;std::size_t profile_bytes=0;
    for(std::size_t at=2;at<input.size();){
        if(input[at++]!=255)break;while(at<input.size()&&input[at]==255)++at;if(at>=input.size())break;
        const auto marker=input[at++];if(marker==0xda||marker==0xd9)break;if(marker==0xd8||(marker>=0xd0&&marker<=0xd7))continue;
        if(input.size()-at<2)break;const auto size=(unsigned(input[at])<<8)|input[at+1];if(size<2||size>input.size()-at)break;
        const auto bytes=input.subspan(at+2,size-2);at+=size;
        if(marker==0xe0&&bytes.size()>=12&&std::memcmp(bytes.data(),"JFIF\0",5)==0){
            const auto x=(unsigned(bytes[8])<<8)|bytes[9],y=(unsigned(bytes[10])<<8)|bytes[11];
            const auto scale=bytes[7]==1?1/.0254:bytes[7]==2?100:0;
            metadata.dots_per_meter_x=std::uint32_t(std::lround(x*scale));metadata.dots_per_meter_y=std::uint32_t(std::lround(y*scale));
        }
        if(marker==0xe1&&bytes.size()>=6&&std::memcmp(bytes.data(),"Exif\0\0",6)==0)metadata.orientation=exifOrientation(bytes.subspan(6));
        if(marker==0xe2&&bytes.size()>=14&&std::memcmp(bytes.data(),"ICC_PROFILE\0",12)==0){
            const auto sequence=bytes[12],count=bytes[13];if(!count||!sequence||sequence>count||(total&&total!=count)||!parts[sequence].empty())return fail(ErrorCode::codec_failure,"Invalid JPEG profile sequence");
            if(bytes.size()-14>ProfileLimit-profile_bytes)return fail(ErrorCode::limit_exceeded,"JPEG profile exceeds metadata budget");
            total=count;profile_bytes+=bytes.size()-14;parts[sequence].assign(bytes.begin()+14,bytes.end());
        }
    }
    if(total){for(unsigned i=1;i<=total;++i){if(parts[i].empty())return fail(ErrorCode::codec_failure,"Incomplete JPEG profile");metadata.icc_profile.insert(metadata.icc_profile.end(),parts[i].begin(),parts[i].end());}}
    return {};
}
Result<void> webpMetadata(std::span<const std::uint8_t> input,Metadata& metadata){
    for(std::size_t at=12;at<=input.size()&&input.size()-at>=8;){const auto size=std::size_t(le32(input.data()+at+4));if(size>input.size()-at-8)return fail(ErrorCode::codec_failure,"Invalid WebP chunk size");
        const auto data=input.subspan(at+8,size);
        if(std::memcmp(input.data()+at,"ICCP",4)==0){if(size>ProfileLimit)return fail(ErrorCode::limit_exceeded,"WebP profile exceeds metadata budget");metadata.icc_profile.assign(data.begin(),data.end());}
        if(std::memcmp(input.data()+at,"EXIF",4)==0)metadata.orientation=exifOrientation(data.size()>=6&&std::memcmp(data.data(),"Exif\0\0",6)==0?data.subspan(6):data);
        at+=8+size+(size&1);
    }
    return {};
}
void orient(Image& image){
    const auto orientation=image.metadata.orientation;if(orientation<=1||orientation>8){image.metadata.orientation=1;return;}
    const auto w=image.width,h=image.height,dw=orientation>=5?h:w,dh=orientation>=5?w:h;std::vector<std::uint8_t> output(image.rgba.size());
    for(std::uint32_t y=0;y<h;++y)for(std::uint32_t x=0;x<w;++x){std::uint32_t dx=x,dy=y;
        switch(orientation){case 2:dx=w-1-x;break;case 3:dx=w-1-x;dy=h-1-y;break;case 4:dy=h-1-y;break;
            case 5:dx=y;dy=x;break;case 6:dx=h-1-y;dy=x;break;case 7:dx=h-1-y;dy=w-1-x;break;case 8:dx=y;dy=w-1-x;break;}
        std::copy_n(image.rgba.data()+(std::size_t(y)*w+x)*4,4,output.data()+(std::size_t(dy)*dw+dx)*4);}
    image.width=dw;image.height=dh;image.rgba=std::move(output);image.metadata.orientation=1;
    if(orientation>=5)std::swap(image.metadata.dots_per_meter_x,image.metadata.dots_per_meter_y);
}
}
Result<Image> decode_metadata(std::span<const std::uint8_t> input,Image image,Format format){
    Result<void> result;
    if(format==Format::png)result=pngMetadata(input,image.metadata);
    else if(format==Format::jpeg)result=jpegMetadata(input,image.metadata);
    else if(format==Format::webp)result=webpMetadata(input,image.metadata);
    else if(format==Format::bmp&&input.size()>=46){image.metadata.dots_per_meter_x=le32(input.data()+38);image.metadata.dots_per_meter_y=le32(input.data()+42);}
    if(!result)return std::unexpected(result.error());orient(image);return image;
}
Result<std::vector<std::uint8_t>> encode_metadata(std::vector<std::uint8_t> input,Format format,const Metadata* metadata,Limits limits){
    if(!metadata)return input;const auto& m=*metadata;
    if(m.icc_profile.size()>ProfileLimit)return fail(ErrorCode::limit_exceeded,"ICC profile exceeds metadata budget");
    if(m.orientation!=1)return fail(ErrorCode::invalid_input,"Encode normalized pixels with orientation 1");
    std::vector<std::uint8_t> output;
    if(format==Format::png&&(m.dots_per_meter_x||m.dots_per_meter_y||!m.icc_profile.empty())){
        if(input.size()<33)return fail(ErrorCode::codec_failure,"Invalid encoded PNG");output.insert(output.end(),input.begin(),input.begin()+33);
        if(m.dots_per_meter_x&&m.dots_per_meter_y){std::vector<std::uint8_t> density;put32(density,m.dots_per_meter_x);put32(density,m.dots_per_meter_y);density.push_back(1);pngChunk(output,"pHYs",density);}
        if(!m.icc_profile.empty()){uLongf size=compressBound(uLong(m.icc_profile.size()));std::vector<std::uint8_t> compressed(size);
            if(compress2(compressed.data(),&size,m.icc_profile.data(),uLong(m.icc_profile.size()),Z_DEFAULT_COMPRESSION)!=Z_OK)return fail(ErrorCode::codec_failure,"Cannot encode ICC profile");compressed.resize(size);
            std::vector<std::uint8_t> profile{'i','c','c',0,0};profile.insert(profile.end(),compressed.begin(),compressed.end());pngChunk(output,"iCCP",profile);}
        output.insert(output.end(),input.begin()+33,input.end());
    }else if(format==Format::jpeg){
        if(m.dots_per_meter_x&&m.dots_per_meter_y&&input.size()>=18&&std::memcmp(input.data()+6,"JFIF\0",5)==0){input[13]=1;
            const auto x=std::clamp(std::lround(m.dots_per_meter_x*.0254),1l,65535l),y=std::clamp(std::lround(m.dots_per_meter_y*.0254),1l,65535l);
            input[14]=std::uint8_t(x>>8);input[15]=std::uint8_t(x);input[16]=std::uint8_t(y>>8);input[17]=std::uint8_t(y);}
        if(m.icc_profile.empty())return input;output={255,216};constexpr std::size_t per=65519;const auto count=(m.icc_profile.size()+per-1)/per;
        for(std::size_t i=0;i<count;++i){const auto n=std::min(per,m.icc_profile.size()-i*per);output.insert(output.end(),{255,226,std::uint8_t((n+16)>>8),std::uint8_t(n+16)});
            const char tag[]="ICC_PROFILE";output.insert(output.end(),tag,tag+12);output.push_back(std::uint8_t(i+1));output.push_back(std::uint8_t(count));
            output.insert(output.end(),m.icc_profile.begin()+i*per,m.icc_profile.begin()+i*per+n);}
        output.insert(output.end(),input.begin()+2,input.end());
    }else if(format==Format::webp&&!m.icc_profile.empty()){
        WebPBitstreamFeatures features{};if(WebPGetFeatures(input.data(),input.size(),&features)!=VP8_STATUS_OK)return fail(ErrorCode::codec_failure,"Invalid encoded WebP");
        output.insert(output.end(),{'R','I','F','F',0,0,0,0,'W','E','B','P','V','P','8','X'});little32(output,10);
        output.push_back(std::uint8_t(0x20|(features.has_alpha?0x10:0)));output.insert(output.end(),3,0);
        for(auto value:{features.width-1,features.height-1})for(int i=0;i<3;++i)output.push_back(std::uint8_t(value>>(i*8)));
        output.insert(output.end(),{'I','C','C','P'});little32(output,std::uint32_t(m.icc_profile.size()));output.insert(output.end(),m.icc_profile.begin(),m.icc_profile.end());if(m.icc_profile.size()&1)output.push_back(0);
        output.insert(output.end(),input.begin()+12,input.end());const auto size=std::uint32_t(output.size()-8);for(int i=0;i<4;++i)output[4+i]=std::uint8_t(size>>(i*8));
    }else return input;
    if(output.size()>limits.output_bytes)return fail(ErrorCode::limit_exceeded,"Encoded metadata exceeds output byte budget");return output;
}
}
