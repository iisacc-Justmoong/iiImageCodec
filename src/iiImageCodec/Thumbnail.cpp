#include "ImageCodec.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace ii::image {
Result<Image> thumbnail(ImageView source,std::uint32_t edge,bool circular,Limits limits) {
    const std::size_t channels=source.format==PixelFormat::rgb8?3:4;
    if(!source.width||!source.height||!edge||source.width>limits.dimension||source.height>limits.dimension||edge>limits.dimension)
        return std::unexpected(Error{ErrorCode::limit_exceeded,"Thumbnail dimensions exceed the configured limit"});
    if(source.metadata&&source.metadata->orientation!=1)
        return std::unexpected(Error{ErrorCode::invalid_input,"Normalize image orientation before creating a thumbnail"});
    const auto row=std::size_t(source.width)*channels,stride=source.row_stride?source.row_stride:row;
    if(stride<row||source.height>std::numeric_limits<std::size_t>::max()/stride||
       (source.height-1)*stride+row>source.pixels.size())
        return std::unexpected(Error{ErrorCode::invalid_input,"Thumbnail source has an invalid pixel layout"});
    if(std::uint64_t(edge)*edge*4>limits.pixel_bytes||std::uint64_t(source.width)*source.height*channels>limits.pixel_bytes)
        return std::unexpected(Error{ErrorCode::limit_exceeded,"Thumbnail pixel storage exceeds the configured limit"});
    Image result{edge,edge,std::vector<std::uint8_t>(std::size_t(edge)*edge*4)};
    if(source.metadata)result.metadata=*source.metadata;
    const double side=std::min(source.width,source.height),left=(source.width-side)/2,top=(source.height-side)/2;
    for(std::uint32_t y=0;y<edge;++y)for(std::uint32_t x=0;x<edge;++x) {
        const double sx=std::clamp(left+(x+.5)*side/edge-.5,0.,double(source.width-1));
        const double sy=std::clamp(top+(y+.5)*side/edge-.5,0.,double(source.height-1));
        const auto x0=std::uint32_t(sx),y0=std::uint32_t(sy),x1=std::min(x0+1,source.width-1),y1=std::min(y0+1,source.height-1);
        const double fx=sx-x0,fy=sy-y0;double color[3]{},alpha=0;
        const auto sample=[&](std::uint32_t px,std::uint32_t py,double weight){auto pixel=source.pixels.data()+py*stride+px*channels;const auto a=channels==4?pixel[3]/255.:1.;alpha+=a*weight;for(int c=0;c<3;++c)color[c]+=pixel[c]*a*weight;};
        sample(x0,y0,(1-fx)*(1-fy));sample(x1,y0,fx*(1-fy));sample(x0,y1,(1-fx)*fy);sample(x1,y1,fx*fy);
        const double coverage=circular?std::clamp(edge/2.+.5-std::hypot(x+.5-edge/2.,y+.5-edge/2.),0.,1.):1;
        auto* pixel=result.rgba.data()+(std::size_t(y)*edge+x)*4;
        for(int c=0;c<3;++c)pixel[c]=alpha>0?std::uint8_t(std::clamp(std::lround(color[c]/alpha),0L,255L)):0;
        pixel[3]=std::uint8_t(std::clamp(std::lround(alpha*coverage*255),0L,255L));
    }
    return result;
}
}
