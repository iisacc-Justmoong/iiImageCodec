#include "OpenCVBridge.h"
#include "Pixel_p.h"
#include <opencv2/core/version.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <climits>
#include <cmath>
#include <cstring>
#include <mutex>

namespace ii::visual::opencv {
namespace {
auto invalid(std::string message){return detail::error(image::ErrorCode::invalid_input,std::move(message));}
auto exceeded(std::string message){return detail::error(image::ErrorCode::limit_exceeded,std::move(message));}
auto failed(const cv::Exception& exception){return detail::error(image::ErrorCode::codec_failure,exception.what());}
int channels(PixelOrder order){switch(order){case PixelOrder::rgb:case PixelOrder::bgr:return 3;case PixelOrder::rgba:case PixelOrder::bgra:return 4;case PixelOrder::gray:return 1;}return 0;}
Result<void> dimensions(std::uint32_t width,std::uint32_t height,image::Limits limits){
    if(!width||!height)return invalid("OpenCV image dimensions are empty");
    if(width>limits.dimension||height>limits.dimension||width>INT_MAX/4||height>INT_MAX||std::uint64_t(width)*height>limits.pixel_bytes/4)
        return exceeded("OpenCV dimensions exceed pixel or backend limits");return {};
}
int interpolation(Interpolation mode){switch(mode){case Interpolation::nearest:return cv::INTER_NEAREST;case Interpolation::linear:return cv::INTER_LINEAR;case Interpolation::area:return cv::INTER_AREA;case Interpolation::cubic:return cv::INTER_CUBIC;}return -1;}
}
std::string backend_version(){return CV_VERSION;}
Result<cv::Mat> to_mat(image::ImageView view,PixelOrder order,image::Limits limits)try{
    auto valid=dimensions(view.width,view.height,limits);if(!valid)return std::unexpected(valid.error());
    auto stride=detail::stride(view,limits);if(!stride)return std::unexpected(stride.error());
    if(!channels(order))return invalid("Unknown OpenCV channel order");
    const auto rgba=view.format==image::PixelFormat::rgba8;
    cv::Mat source(int(view.height),int(view.width),rgba?CV_8UC4:CV_8UC3,const_cast<std::uint8_t*>(view.pixels.data()),*stride),result;
    int code=-1;
    switch(order){
    case PixelOrder::rgb:if(rgba)code=cv::COLOR_RGBA2RGB;break;
    case PixelOrder::bgr:code=rgba?cv::COLOR_RGBA2BGR:cv::COLOR_RGB2BGR;break;
    case PixelOrder::rgba:if(!rgba)code=cv::COLOR_RGB2RGBA;break;
    case PixelOrder::bgra:code=rgba?cv::COLOR_RGBA2BGRA:cv::COLOR_RGB2BGRA;break;
    case PixelOrder::gray:code=rgba?cv::COLOR_RGBA2GRAY:cv::COLOR_RGB2GRAY;break;
    }
    if(code<0)result=source.clone();else cv::cvtColor(source,result,code);
    return result;
}catch(const cv::Exception& error){return failed(error);}catch(const std::bad_alloc&){return exceeded("OpenCV matrix allocation failed");}
Result<image::Image> from_mat(const cv::Mat& source,PixelOrder order,image::Limits limits)try{
    const auto count=channels(order);
    if(source.empty()||source.dims!=2||source.depth()!=CV_8U||!count||source.channels()!=count)return invalid("Mat must have 2 dimensions, uint8 depth and channels matching its explicit order");
    auto valid=dimensions(source.cols,source.rows,limits);if(!valid)return std::unexpected(valid.error());
    cv::Mat rgba;int code=-1;
    switch(order){case PixelOrder::rgb:code=cv::COLOR_RGB2RGBA;break;case PixelOrder::bgr:code=cv::COLOR_BGR2RGBA;break;
    case PixelOrder::rgba:break;case PixelOrder::bgra:code=cv::COLOR_BGRA2RGBA;break;case PixelOrder::gray:code=cv::COLOR_GRAY2RGBA;break;}
    if(code<0)rgba=source;else cv::cvtColor(source,rgba,code);
    image::Image image{std::uint32_t(source.cols),std::uint32_t(source.rows),std::vector<std::uint8_t>(std::size_t(source.cols)*source.rows*4)};
    for(int y=0;y<rgba.rows;++y)std::memcpy(image.rgba.data()+std::size_t(y)*rgba.cols*4,rgba.ptr(y),std::size_t(rgba.cols)*4);
    return image;
}catch(const cv::Exception& error){return failed(error);}catch(const std::bad_alloc&){return exceeded("OpenCV image allocation failed");}
Result<image::Image> resize(image::ImageView source,std::uint32_t width,std::uint32_t height,Interpolation mode,image::Limits limits)try{
    auto valid=dimensions(width,height,limits);if(!valid)return std::unexpected(valid.error());const auto method=interpolation(mode);if(method<0)return invalid("Unknown OpenCV resize interpolation");
    auto mat=to_mat(source,PixelOrder::rgba,limits);if(!mat)return std::unexpected(mat.error());cv::Mat scaled;
    cv::resize(*mat,scaled,cv::Size(int(width),int(height)),0,0,method);auto image=from_mat(scaled,PixelOrder::rgba,limits);if(image&&source.metadata)image->metadata=*source.metadata;return image;
}catch(const cv::Exception& error){return failed(error);}catch(const std::bad_alloc&){return exceeded("OpenCV resize allocation failed");}
Result<image::Image> grayscale(image::ImageView source,image::Limits limits){
    auto gray=to_mat(source,PixelOrder::gray,limits);if(!gray)return std::unexpected(gray.error());auto result=from_mat(*gray,PixelOrder::gray,limits);
    if(result){
        if(source.metadata)result->metadata=*source.metadata;
        if(source.format==image::PixelFormat::rgba8){const auto stride=source.row_stride?source.row_stride:std::size_t(source.width)*4;
            for(std::uint32_t y=0;y<source.height;++y)for(std::uint32_t x=0;x<source.width;++x)result->rgba[(std::size_t(y)*source.width+x)*4+3]=source.pixels[y*stride+x*4+3];}
    }
    return result;
}
Result<vision::Mask> edges(image::ImageView source,EdgeOptions options,vision::Limits limits)try{
    if(!std::isfinite(options.low)||!std::isfinite(options.high)||options.low<0||options.high<options.low||
       (options.aperture!=3&&options.aperture!=5&&options.aperture!=7))return invalid("Invalid Canny thresholds or aperture");
    auto gray=to_mat(source,PixelOrder::gray,limits.image);if(!gray)return std::unexpected(gray.error());
    if(std::uint64_t(source.width)*source.height>limits.tensor_bytes)return exceeded("Edge mask exceeds byte budget");cv::Mat output;
    cv::Canny(*gray,output,options.low,options.high,options.aperture,options.l2_gradient);
    vision::Mask result{source.width,source.height,std::vector<std::uint8_t>(std::size_t(source.width)*source.height)};
    for(int y=0;y<output.rows;++y)std::memcpy(result.values.data()+std::size_t(y)*output.cols,output.ptr(y),output.cols);return result;
}catch(const cv::Exception& error){return failed(error);}catch(const std::bad_alloc&){return exceeded("Canny allocation failed");}
Result<std::vector<vision::Object>> regions(const vision::Mask& mask,RegionOptions options,vision::Limits limits)try{
    auto valid=vision::validate(mask,limits);if(!valid)return std::unexpected(valid.error());
    if((options.connectivity!=4&&options.connectivity!=8)||!options.minimum_area||(options.maximum_area&&options.maximum_area<options.minimum_area))return invalid("Invalid connected-region settings");
    if(mask.width>INT_MAX||mask.height>INT_MAX||std::uint64_t(mask.width)*mask.height>limits.image.pixel_bytes/sizeof(int))return exceeded("Connected component label image exceeds byte budget");
    cv::Mat input(int(mask.height),int(mask.width),CV_8U,const_cast<std::uint8_t*>(mask.values.data())),labels,stats,centres;
    const auto count=cv::connectedComponentsWithStats(input,labels,stats,centres,options.connectivity,CV_32S);
    std::vector<vision::Object> result;
    for(int i=1;i<count;++i){const auto area=std::size_t(stats.at<int>(i,cv::CC_STAT_AREA));if(area<options.minimum_area||(options.maximum_area&&area>options.maximum_area))continue;
        if(result.size()>=limits.objects)return exceeded("Connected region count exceeds budget");
        vision::Object object;object.class_id=0;object.confidence=1;object.label="region";
        object.box={float(stats.at<int>(i,cv::CC_STAT_LEFT)),float(stats.at<int>(i,cv::CC_STAT_TOP)),float(stats.at<int>(i,cv::CC_STAT_WIDTH)),float(stats.at<int>(i,cv::CC_STAT_HEIGHT))};result.push_back(std::move(object));}
    return result;
}catch(const cv::Exception& error){return failed(error);}catch(const std::bad_alloc&){return exceeded("Connected region allocation failed");}
Result<vision::FeatureSet> orb_features(image::ImageView source,std::size_t maximum,vision::Limits limits)try{
    if(!maximum)return invalid("ORB maximum feature count is zero");if(maximum>limits.keypoints||maximum>INT_MAX||maximum>limits.tensor_bytes/32)return exceeded("ORB descriptor budget exceeded");
    auto gray=to_mat(source,PixelOrder::gray,limits.image);if(!gray)return std::unexpected(gray.error());
    auto orb=cv::ORB::create(int(maximum));std::vector<cv::KeyPoint> points;cv::Mat descriptors;orb->detectAndCompute(*gray,cv::noArray(),points,descriptors);
    vision::FeatureSet result;result.descriptor_bytes=points.empty()?0:std::size_t(descriptors.cols);
    if(points.size()>limits.keypoints||descriptors.total()>limits.tensor_bytes)return exceeded("ORB output exceeds feature budget");
    for(const auto& p:points)result.keypoints.push_back({{p.pt.x,p.pt.y},1,{}});
    if(!descriptors.empty())result.descriptors.assign(descriptors.ptr<std::uint8_t>(),descriptors.ptr<std::uint8_t>()+descriptors.total());return result;
}catch(const cv::Exception& error){return failed(error);}catch(const std::bad_alloc&){return exceeded("ORB allocation failed");}
Result<std::vector<vision::TrackedPoint>> track_features(image::ImageView previous,image::ImageView current,std::span<const vision::Point> points,FlowOptions options,vision::Limits limits)try{
    if(previous.width!=current.width||previous.height!=current.height||options.window<3||options.window>255||options.window%2==0||options.levels>8||
       !std::isfinite(options.minimum_eigenvalue)||options.minimum_eigenvalue<0)return invalid("Invalid optical flow frames or settings");
    if(points.size()>limits.keypoints||points.size()>INT_MAX||points.size()>limits.tensor_bytes/sizeof(vision::TrackedPoint))return exceeded("Optical flow point budget exceeded");
    std::vector<cv::Point2f> from;for(const auto& p:points){if(!std::isfinite(p.x)||!std::isfinite(p.y)||p.x<0||p.y<0||p.x>=previous.width||p.y>=previous.height)return invalid("Optical flow input point is outside the frame");from.emplace_back(p.x,p.y);}
    auto a=to_mat(previous,PixelOrder::gray,limits.image),b=to_mat(current,PixelOrder::gray,limits.image);if(!a)return std::unexpected(a.error());if(!b)return std::unexpected(b.error());
    if(points.empty())return std::vector<vision::TrackedPoint>{};
    std::vector<cv::Point2f> to;std::vector<std::uint8_t> status;std::vector<float> errors;
    cv::calcOpticalFlowPyrLK(*a,*b,from,to,status,errors,cv::Size(options.window,options.window),int(options.levels),cv::TermCriteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,30,0.01),0,options.minimum_eigenvalue);
    std::vector<vision::TrackedPoint> result;for(std::size_t i=0;i<points.size();++i){const bool found=status[i]&&std::isfinite(to[i].x)&&std::isfinite(to[i].y)&&std::isfinite(errors[i])&&to[i].x>=0&&to[i].y>=0&&to[i].x<current.width&&to[i].y<current.height;
        result.push_back({found?vision::Point{to[i].x,to[i].y}:points[i],found,found?errors[i]:0});}return result;
}catch(const cv::Exception& error){return failed(error);}catch(const std::bad_alloc&){return exceeded("Optical flow allocation failed");}
Result<image::Image> draw_objects(image::ImageView source,std::span<const vision::Object> objects,std::array<std::uint8_t,3> rgb,unsigned thickness,vision::Limits limits)try{
    if(!thickness||thickness>255)return invalid("Invalid object outline thickness");if(objects.size()>limits.objects)return exceeded("Object drawing count exceeds budget");
    for(const auto& object:objects){auto valid=vision::validate(object,source.width,source.height,limits);if(!valid)return std::unexpected(valid.error());}
    auto mat=to_mat(source,PixelOrder::rgba,limits.image);if(!mat)return std::unexpected(mat.error());
    for(const auto& object:objects){const auto& box=object.box;cv::rectangle(*mat,cv::Point(int(std::floor(box.x)),int(std::floor(box.y))),
        cv::Point(int(std::ceil(box.x+box.width))-1,int(std::ceil(box.y+box.height))-1),cv::Scalar(rgb[0],rgb[1],rgb[2],255),int(thickness));}
    auto image=from_mat(*mat,PixelOrder::rgba,limits.image);if(image&&source.metadata)image->metadata=*source.metadata;return image;
}catch(const cv::Exception& error){return failed(error);}catch(const std::bad_alloc&){return exceeded("Object drawing allocation failed");}
Result<vision::PreparedInput> Preprocessor::prepare(image::ImageView source,const vision::PreprocessOptions& options,vision::Limits limits)const try{
    auto valid=vision::validate(options,limits);if(!valid)return std::unexpected(valid.error());valid=dimensions(options.width,options.height,limits.image);if(!valid)return std::unexpected(valid.error());
    auto input=to_mat(source,PixelOrder::rgb,limits.image);if(!input)return std::unexpected(input.error());
    int width=int(options.width),height=int(options.height),pad_x=0,pad_y=0;
    if(options.resize==vision::ResizeMode::letterbox){const double scale=std::min(double(options.width)/source.width,double(options.height)/source.height);
        width=std::clamp(int(std::lround(source.width*scale)),1,int(options.width));height=std::clamp(int(std::lround(source.height*scale)),1,int(options.height));pad_x=(int(options.width)-width)/2;pad_y=(int(options.height)-height)/2;}
    cv::Mat scaled;cv::resize(*input,scaled,cv::Size(width,height),0,0,cv::INTER_LINEAR);
    cv::Mat padded(int(options.height),int(options.width),CV_8UC3,cv::Scalar(options.border[0],options.border[1],options.border[2]));scaled.copyTo(padded(cv::Rect(pad_x,pad_y,width,height)));
    vision::PreparedInput result;result.transform={source.width,source.height,options.width,options.height,float(width)/source.width,float(height)/source.height,float(pad_x),float(pad_y)};
    result.tensor.shape={1,3,options.height,options.width};const auto plane=std::size_t(options.width)*options.height;result.tensor.values.resize(plane*3);
    for(std::uint32_t y=0;y<options.height;++y)for(std::uint32_t x=0;x<options.width;++x)for(unsigned c=0;c<3;++c){
        const auto channel=options.channels==vision::ChannelOrder::rgb?c:2-c;
        const auto value=(float(padded.at<cv::Vec3b>(int(y),int(x))[channel])*options.scale-options.mean[c])/options.standard_deviation[c];
        if(!std::isfinite(value))return invalid("Normalization produced a nonfinite tensor");result.tensor.values[c*plane+std::size_t(y)*options.width+x]=value;}
    return result;
}catch(const cv::Exception& error){return failed(error);}catch(const std::bad_alloc&){return exceeded("Vision preprocessing allocation failed");}
}
