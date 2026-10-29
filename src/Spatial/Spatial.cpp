#include "Spatial.h"
#include "Visual_p.h"
#include <numbers>

namespace ii::visual::spatial {
Result<void> validate(const Metadata& metadata) {
    if ((metadata.projection != Projection::rectilinear && metadata.projection != Projection::equirectangular) ||
        (metadata.stereo != StereoLayout::mono && metadata.stereo != StereoLayout::side_by_side && metadata.stereo != StereoLayout::top_bottom) ||
        !detail::valid_pose(metadata.pose))
        return detail::error(image::ErrorCode::invalid_input, "Invalid spatial projection, stereo layout or pose");
    return {};
}
Result<image::Image> extract_eye(image::ImageView view, StereoLayout layout, Eye eye, image::Limits limits) try {
    const auto step=detail::stride(view,limits); if(!step) return std::unexpected(step.error());
    if ((eye!=Eye::left && eye!=Eye::right) || (layout!=StereoLayout::mono && layout!=StereoLayout::side_by_side && layout!=StereoLayout::top_bottom))
        return detail::error(image::ErrorCode::invalid_input,"Invalid stereo layout or eye");
    if ((layout==StereoLayout::side_by_side && view.width%2) || (layout==StereoLayout::top_bottom && view.height%2))
        return detail::error(image::ErrorCode::invalid_input,"Packed stereo dimension must be even");
    auto width=view.width/(layout==StereoLayout::side_by_side?2:1);
    auto height=view.height/(layout==StereoLayout::top_bottom?2:1);
    if(!width || !height) return detail::error(image::ErrorCode::invalid_input,"Empty eye image");
    const unsigned channels=view.format==image::PixelFormat::rgb8?3:4;
    const auto offset=(eye==Eye::right ? (layout==StereoLayout::side_by_side?std::size_t(width)*channels:layout==StereoLayout::top_bottom?height**step:0) : 0);
    return detail::rgba_copy({width,height,view.format,*step,view.pixels.subspan(offset),view.metadata},*step);
} catch(const std::bad_alloc&) {return detail::error(image::ErrorCode::limit_exceeded,"Stereo allocation failed");}

Result<image::Image> pack_stereo(image::ImageView left,image::ImageView right,StereoLayout layout,image::Limits limits) try {
    auto ls=detail::stride(left,limits),rs=detail::stride(right,limits);
    if(!ls)return std::unexpected(ls.error());if(!rs)return std::unexpected(rs.error());
    if(left.width!=right.width || left.height!=right.height || (layout!=StereoLayout::side_by_side && layout!=StereoLayout::top_bottom))
        return detail::error(image::ErrorCode::invalid_input,"Stereo eyes must have equal dimensions and a packed layout");
    const auto width=std::uint64_t(left.width)*(layout==StereoLayout::side_by_side?2:1);
    const auto height=std::uint64_t(left.height)*(layout==StereoLayout::top_bottom?2:1);
    if(width>limits.dimension || height>limits.dimension || width*height>limits.pixel_bytes/4)
        return detail::error(image::ErrorCode::limit_exceeded,"Packed stereo exceeds pixel budget");
    image::Image result{std::uint32_t(width),std::uint32_t(height),std::vector<std::uint8_t>(width*height*4)};
    if(left.metadata)result.metadata=*left.metadata;
    const auto l=detail::rgba_copy(left,*ls),r=detail::rgba_copy(right,*rs);
    for(unsigned eye=0;eye<2;++eye)for(std::uint32_t y=0;y<left.height;++y){
        const auto dx=layout==StereoLayout::side_by_side?eye*left.width:0;
        const auto dy=layout==StereoLayout::top_bottom?eye*left.height:0;
        const auto& source=eye?r:l;
        std::copy_n(source.rgba.data()+std::size_t(y)*left.width*4,std::size_t(left.width)*4,
                    result.rgba.data()+(std::size_t(y+dy)*result.width+dx)*4);
    }
    return result;
}catch(const std::bad_alloc&){return detail::error(image::ErrorCode::limit_exceeded,"Stereo allocation failed");}

Result<image::Image> render_view(image::ImageView source,const Metadata& metadata,const ViewOptions& options,image::Limits limits) try {
    auto valid=validate(metadata);if(!valid)return std::unexpected(valid.error());
    if(metadata.projection!=Projection::equirectangular)
        return detail::error(image::ErrorCode::unsupported_format,"Viewport requires an equirectangular panorama");
    if(!detail::valid_pose(options.orientation) || !std::isfinite(options.horizontal_fov_degrees) || options.horizontal_fov_degrees<=0 || options.horizontal_fov_degrees>=180)
        return detail::error(image::ErrorCode::invalid_input,"Invalid viewport orientation or field of view");
    if(!options.width || !options.height || options.width>limits.dimension || options.height>limits.dimension ||
        std::uint64_t(options.width)*options.height>limits.pixel_bytes/4)
        return detail::error(image::ErrorCode::limit_exceeded,"Viewport dimensions exceed pixel budget");
    auto eye=extract_eye(source,metadata.stereo,options.eye,limits);if(!eye)return std::unexpected(eye.error());
    if(std::uint64_t(eye->height)*2!=eye->width)
        return detail::error(image::ErrorCode::invalid_input,"Each equirectangular eye must have a 2:1 aspect ratio");
    image::Image result{options.width,options.height,std::vector<std::uint8_t>(std::size_t(options.width)*options.height*4),eye->metadata};
    const auto scale=std::tan(options.horizontal_fov_degrees*std::numbers::pi/360);
    for(std::uint32_t y=0;y<options.height;++y)for(std::uint32_t x=0;x<options.width;++x){
        auto ray=detail::rotate({(2*(x+0.5)/options.width-1)*scale,(1-2*(y+0.5)/options.height)*scale*options.height/options.width,-1},options.orientation.rotation);
        ray=detail::rotate(ray,metadata.pose.rotation,true);
        const double length=std::hypot(ray[0],ray[1],ray[2]);
        const double u=std::atan2(ray[0],-ray[2])/(2*std::numbers::pi)+0.5;
        const double v=std::acos(std::clamp(ray[1]/length,-1.0,1.0))/std::numbers::pi;
        const double sx=u*eye->width-0.5,sy=v*eye->height-0.5;
        const auto ix=std::int64_t(std::floor(sx)),iy=std::int64_t(std::floor(sy));
        double values[4]{};
        for(unsigned j=0;j<2;++j)for(unsigned i=0;i<2;++i){
            auto px=(ix+i)%eye->width;if(px<0)px+=eye->width;
            const auto py=std::clamp<std::int64_t>(iy+j,0,eye->height-1);
            const auto* p=eye->rgba.data()+(std::size_t(py)*eye->width+px)*4;
            const auto weight=(i?sx-ix:1-(sx-ix))*(j?sy-iy:1-(sy-iy));
            values[3]+=p[3]*weight;
            for(unsigned c=0;c<3;++c)values[c]+=p[c]*p[3]/255.0*weight;
        }
        auto* p=result.rgba.data()+(std::size_t(y)*options.width+x)*4;
        p[3]=std::uint8_t(std::clamp(std::lround(values[3]),0l,255l));
        for(unsigned c=0;c<3;++c)p[c]=values[3]>0?std::uint8_t(std::clamp(std::lround(values[c]*255/values[3]),0l,255l)):0;
    }
    return result;
}catch(const std::bad_alloc&){return detail::error(image::ErrorCode::limit_exceeded,"Viewport allocation failed");}

Result<PointCloud> unproject_depth(DepthView depth,const CameraIntrinsics& camera,const Pose& pose,image::Limits limits)try{
    if(depth.width!=camera.width || depth.height!=camera.height || !depth.width || !depth.height ||
        !std::isfinite(camera.fx)||!std::isfinite(camera.fy)||!std::isfinite(camera.cx)||!std::isfinite(camera.cy)||
        camera.fx<=0 || camera.fy<=0 || !detail::valid_pose(pose))
        return detail::error(image::ErrorCode::invalid_input,"Invalid depth intrinsics, dimensions or pose");
    if(depth.width>limits.dimension || depth.height>limits.dimension || std::uint64_t(depth.width)*depth.height>limits.pixel_bytes/sizeof(std::array<double,3>))
        return detail::error(image::ErrorCode::limit_exceeded,"Depth point cloud exceeds pixel budget");
    const auto step=depth.row_stride?depth.row_stride:depth.width;
    if(step<depth.width || (depth.height>1 && step>(std::numeric_limits<std::size_t>::max()-depth.width)/(depth.height-1)) ||
        (depth.height-1)*step+depth.width>depth.metres.size())
        return detail::error(image::ErrorCode::invalid_input,"Invalid depth stride or extent");
    PointCloud cloud;cloud.points.reserve(std::size_t(depth.width)*depth.height);
    for(std::uint32_t y=0;y<depth.height;++y)for(std::uint32_t x=0;x<depth.width;++x){
        const auto z=depth.metres[y*step+x];if(!std::isfinite(z)||z<=0)continue;
        auto point=detail::rotate({(x-camera.cx)*z/camera.fx,(camera.cy-y)*z/camera.fy,-z},pose.rotation);
        for(unsigned i=0;i<3;++i)point[i]+=pose.translation[i];cloud.points.push_back(point);
    }
    return cloud;
}catch(const std::bad_alloc&){return detail::error(image::ErrorCode::limit_exceeded,"Depth allocation failed");}
}
