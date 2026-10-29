#include "Vision.h"
#include "Pixel_p.h"
#include <cmath>
#include <numeric>

namespace ii::visual::vision {
namespace {
auto invalid(std::string message){return detail::error(image::ErrorCode::invalid_input,std::move(message));}
auto exceeded(std::string message){return detail::error(image::ErrorCode::limit_exceeded,std::move(message));}
bool probability(float x){return std::isfinite(x)&&x>=0&&x<=1;}
bool box_valid(Box b){return std::isfinite(b.x)&&std::isfinite(b.y)&&std::isfinite(b.width)&&std::isfinite(b.height)&&b.x>=0&&b.y>=0&&b.width>0&&b.height>0&&std::isfinite(b.x+b.width)&&std::isfinite(b.y+b.height);}
Result<void> detection_options(DetectionOptions options,Limits limits){
    if(!probability(options.score_threshold)||!probability(options.iou_threshold)||!options.max_objects)return invalid("Invalid score/IoU threshold or detection count");
    if(options.max_objects>limits.objects)return exceeded("Detection output count exceeds budget");return {};
}
Result<void> yolo_spec(YoloSpec spec,std::span<const std::string> labels,Limits limits){
    if(!spec.classes||spec.classes>limits.classes||spec.classes>std::size_t(INT32_MAX))return invalid("Invalid YOLO class count");
    if(!labels.empty()&&labels.size()!=spec.classes)return invalid("YOLO labels must match the class count");
    if((spec.layout!=YoloLayout::channels_first&&spec.layout!=YoloLayout::rows)||
       (spec.boxes!=BoxEncoding::center_xywh&&spec.boxes!=BoxEncoding::corners_xyxy))return invalid("Invalid YOLO layout or box encoding");return {};
}
float sigmoid(float value){return value>=0?1/(1+std::exp(-value)):std::exp(value)/(1+std::exp(value));}
}
Result<void> validate(const Tensor& tensor,Limits limits){
    if(tensor.shape.empty()||tensor.shape.size()>8)return invalid("Tensor requires a rank from 1 to 8");
    std::size_t count=1;
    for(auto dimension:tensor.shape){
        if(dimension<=0)return invalid("Tensor dimensions must be positive");
        if(std::uint64_t(dimension)>limits.tensor_bytes/sizeof(float)/count)return exceeded("Tensor exceeds float byte budget");
        count*=std::size_t(dimension);
    }
    if(count!=tensor.values.size())return invalid("Tensor shape does not match its values");
    for(auto value:tensor.values)if(!std::isfinite(value))return invalid("Tensor contains a nonfinite value");return {};
}
Result<void> validate(const Mask& mask,Limits limits){
    if(!mask.width||!mask.height)return invalid("Mask dimensions are empty");
    const auto count=std::uint64_t(mask.width)*mask.height;
    if(mask.width>limits.image.dimension||mask.height>limits.image.dimension||count>limits.tensor_bytes)return exceeded("Mask exceeds dimension or byte budget");
    if(count!=mask.values.size())return invalid("Mask dimensions do not match its values");return {};
}
Result<void> validate(const Object& object,std::uint32_t width,std::uint32_t height,Limits limits){
    if(!width||!height||object.class_id<0||std::size_t(object.class_id)>=limits.classes||!probability(object.confidence)||!box_valid(object.box)||
       object.box.x+object.box.width>width||object.box.y+object.box.height>height)return invalid("Invalid object class, score or source-pixel box");
    if(object.keypoints.size()>limits.keypoints)return exceeded("Object keypoint count exceeds budget");
    for(const auto& key:object.keypoints)if(!probability(key.confidence)||!std::isfinite(key.point.x)||!std::isfinite(key.point.y)||
        key.point.x<0||key.point.y<0||key.point.x>width||key.point.y>height)return invalid("Invalid object keypoint");
    if(object.mask)return validate(*object.mask,limits);return {};
}
Result<void> validate(const PreprocessOptions& options,Limits limits){
    if(!options.width||!options.height||!std::isfinite(options.scale)||options.scale<=0||
       (options.resize!=ResizeMode::stretch&&options.resize!=ResizeMode::letterbox)||
       (options.channels!=ChannelOrder::rgb&&options.channels!=ChannelOrder::bgr))return invalid("Invalid vision input size, scale or preprocessing mode");
    if(options.width>limits.image.dimension||options.height>limits.image.dimension||
       std::uint64_t(options.width)*options.height>limits.tensor_bytes/(3*sizeof(float)))return exceeded("Prepared input exceeds dimension or tensor budget");
    for(unsigned c=0;c<3;++c)if(!std::isfinite(options.mean[c])||!std::isfinite(options.standard_deviation[c])||options.standard_deviation[c]<=0)return invalid("Invalid normalization mean or standard deviation");return {};
}
float intersection_over_union(Box a,Box b)noexcept{
    if(!box_valid(a)||!box_valid(b))return 0;
    const double w=std::max(0.0,double(std::min(a.x+a.width,b.x+b.width))-std::max(a.x,b.x));
    const double h=std::max(0.0,double(std::min(a.y+a.height,b.y+b.height))-std::max(a.y,b.y));
    const double intersection=w*h,total=double(a.width)*a.height+double(b.width)*b.height-intersection;
    return total>0?float(intersection/total):0;
}
Result<std::vector<Object>> suppress(std::span<const Object> objects,DetectionOptions options,Limits limits)try{
    auto checked=detection_options(options,limits);if(!checked)return std::unexpected(checked.error());
    if(objects.size()>limits.candidates)return exceeded("NMS candidate count exceeds budget");
    std::vector<std::size_t> order;std::size_t mask_bytes=0,keypoints=0;
    for(std::size_t i=0;i<objects.size();++i){
        const auto& object=objects[i];
        if(!probability(object.confidence)||!box_valid(object.box)||object.class_id<0||std::size_t(object.class_id)>=limits.classes)return invalid("Invalid NMS candidate");
        if(object.keypoints.size()>limits.keypoints-keypoints)return exceeded("NMS keypoints exceed budget");keypoints+=object.keypoints.size();
        for(const auto& key:object.keypoints)if(!std::isfinite(key.point.x)||!std::isfinite(key.point.y)||!probability(key.confidence))return invalid("Invalid NMS keypoint");
        if(object.mask){auto valid=validate(*object.mask,limits);if(!valid)return std::unexpected(valid.error());
            if(object.mask->values.size()>limits.tensor_bytes-mask_bytes)return exceeded("NMS mask bytes exceed budget");mask_bytes+=object.mask->values.size();}
        if(object.confidence>=options.score_threshold)order.push_back(i);
    }
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return objects[a].confidence>objects[b].confidence;});
    std::vector<Object> result;
    for(auto i:order){
        bool duplicate=false;for(const auto& kept:result)if((options.class_agnostic||kept.class_id==objects[i].class_id)&&intersection_over_union(kept.box,objects[i].box)>options.iou_threshold){duplicate=true;break;}
        if(!duplicate){result.push_back(objects[i]);if(result.size()==options.max_objects)break;}
    }
    return result;
}catch(const std::bad_alloc&){return exceeded("Object suppression allocation failed");}
Result<std::vector<Object>> decode_yolo(const Tensor& tensor,const Transform& map,YoloSpec spec,DetectionOptions options,std::span<const std::string> labels,Limits limits)try{
    auto checked=validate(tensor,limits);if(!checked)return std::unexpected(checked.error());
    checked=yolo_spec(spec,labels,limits);if(!checked)return std::unexpected(checked.error());
    checked=detection_options(options,limits);if(!checked)return std::unexpected(checked.error());
    if(!map.source_width||!map.source_height||!map.input_width||!map.input_height||!std::isfinite(map.scale_x)||!std::isfinite(map.scale_y)||
       !std::isfinite(map.pad_x)||!std::isfinite(map.pad_y)||map.scale_x<=0||map.scale_y<=0||map.pad_x<0||map.pad_y<0||
       map.source_width>limits.image.dimension||map.source_height>limits.image.dimension||
       map.pad_x+map.source_width*map.scale_x>map.input_width+.01f||map.pad_y+map.source_height*map.scale_y>map.input_height+.01f)return invalid("Invalid source-to-network coordinate transform");
    if((tensor.shape.size()!=2&&tensor.shape.size()!=3)||(tensor.shape.size()==3&&tensor.shape[0]!=1))return invalid("YOLO output requires a single batch of rows or channels");
    const auto a=std::size_t(tensor.shape[tensor.shape.size()-2]),b=std::size_t(tensor.shape.back());
    const auto channels=spec.classes+4+(spec.objectness?1:0);
    const auto count=spec.layout==YoloLayout::channels_first?b:a;
    if((spec.layout==YoloLayout::channels_first?a:b)!=channels)return invalid("YOLO output channels do not match its explicit specification");
    if(count>limits.candidates)return exceeded("YOLO candidate count exceeds budget");
    auto at=[&](std::size_t row,std::size_t channel){return tensor.values[spec.layout==YoloLayout::channels_first?channel*count+row:row*channels+channel];};
    auto score=[&](float value)->Result<float>{if(spec.scores_are_logits)return sigmoid(value);if(!probability(value))return invalid("YOLO scores must be probabilities or explicitly declared logits");return value;};
    std::vector<Object> objects;
    for(std::size_t row=0;row<count;++row){
        float objectness=1;if(spec.objectness){auto s=score(at(row,4));if(!s)return std::unexpected(s.error());objectness=*s;}
        float best=-1;std::int32_t category=0;
        for(std::size_t c=0;c<spec.classes;++c){auto s=score(at(row,4+(spec.objectness?1:0)+c));if(!s)return std::unexpected(s.error());if(*s>best){best=*s;category=std::int32_t(c);}}
        const float confidence=best*objectness;if(confidence<options.score_threshold)continue;
        double x=at(row,0),y=at(row,1),w=at(row,2),h=at(row,3);
        if(spec.normalized_coordinates){x*=map.input_width;y*=map.input_height;w*=map.input_width;h*=map.input_height;}
        double left=x,top=y,right=w,bottom=h;
        if(spec.boxes==BoxEncoding::center_xywh){if(w<=0||h<=0)continue;left=x-w/2;top=y-h/2;right=x+w/2;bottom=y+h/2;}
        left=std::clamp((left-map.pad_x)/map.scale_x,0.0,double(map.source_width));right=std::clamp((right-map.pad_x)/map.scale_x,0.0,double(map.source_width));
        top=std::clamp((top-map.pad_y)/map.scale_y,0.0,double(map.source_height));bottom=std::clamp((bottom-map.pad_y)/map.scale_y,0.0,double(map.source_height));
        if(right<=left||bottom<=top)continue;
        Object object;object.class_id=category;object.confidence=confidence;object.box={float(left),float(top),float(right-left),float(bottom-top)};
        if(!labels.empty())object.label=labels[std::size_t(category)];objects.push_back(std::move(object));
    }
    return suppress(objects,options,limits);
}catch(const std::bad_alloc&){return exceeded("YOLO output allocation failed");}
Result<std::vector<Classification>> classify(const Tensor& tensor,std::size_t top_k,bool logits,std::span<const std::string> labels,Limits limits)try{
    auto checked=validate(tensor,limits);if(!checked)return std::unexpected(checked.error());
    if(!top_k||tensor.values.empty()||tensor.values.size()>limits.classes||(!labels.empty()&&labels.size()!=tensor.values.size()))return invalid("Invalid classification count or labels");
    std::size_t nonunit=0;for(auto d:tensor.shape)nonunit+=d!=1;if(nonunit>1)return invalid("Classification output must be a single class vector");
    std::vector<double> scores(tensor.values.begin(),tensor.values.end());
    if(logits){double maximum=*std::max_element(scores.begin(),scores.end()),total=0;for(auto& value:scores){value=std::exp(value-maximum);total+=value;}for(auto& value:scores)value/=total;}
    else for(auto value:scores)if(value<0||value>1)return invalid("Classification probabilities must be within 0..1");
    std::vector<std::size_t> order(scores.size());std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return scores[a]>scores[b];});
    std::vector<Classification> result;for(std::size_t i=0;i<std::min(top_k,order.size());++i){const auto c=order[i];result.push_back({std::int32_t(c),float(scores[c]),labels.empty()?std::string{}:labels[c]});}return result;
}catch(const std::bad_alloc&){return exceeded("Classification allocation failed");}
Result<YoloDetector> YoloDetector::create(std::shared_ptr<const InferenceModel> model,std::shared_ptr<const Preprocessor> preprocessing,DetectorConfig config,Limits limits){
    if(!model||!preprocessing)return invalid("Detector requires an inference model and preprocessor");
    auto checked=validate(config.input,limits);if(!checked)return std::unexpected(checked.error());checked=yolo_spec(config.output,config.labels,limits);if(!checked)return std::unexpected(checked.error());
    return YoloDetector(std::move(model),std::move(preprocessing),std::move(config));
}
Result<DetectionResult> YoloDetector::detect(const Frame& frame,DetectionOptions options,Limits limits)const try{
    auto checked=detection_options(options,limits);if(!checked)return std::unexpected(checked.error());
    auto prepared=preprocessor_->prepare(frame.image,config_.input,limits);if(!prepared)return std::unexpected(prepared.error());
    if(prepared->transform.source_width!=frame.image.width||prepared->transform.source_height!=frame.image.height)return invalid("Preprocessor source dimensions do not match the frame");
    checked=validate(prepared->tensor,limits);if(!checked)return std::unexpected(checked.error());
    if(prepared->transform.input_width!=config_.input.width||prepared->transform.input_height!=config_.input.height||
       prepared->tensor.shape!=std::vector<std::int64_t>{1,3,config_.input.height,config_.input.width})return invalid("Preprocessor must return the configured NCHW input tensor and transform");
    auto outputs=model_->run(prepared->tensor,limits);if(!outputs)return std::unexpected(outputs.error());
    const Tensor* tensor=nullptr;
    if(config_.output_name.empty()){if(outputs->size()!=1)return invalid("Multiple model outputs require an explicit output name");tensor=&outputs->front();}
    else for(const auto& output:*outputs)if(output.name==config_.output_name){if(tensor)return invalid("Model output names are not unique");tensor=&output;}
    if(!tensor)return invalid("Requested model output is absent");
    auto objects=decode_yolo(*tensor,prepared->transform,config_.output,options,config_.labels,limits);if(!objects)return std::unexpected(objects.error());
    return DetectionResult{frame.image.width,frame.image.height,frame.id,frame.timestamp_ns,std::move(*objects)};
}catch(const std::bad_alloc&){return exceeded("Detector allocation failed");}catch(const std::exception& error){return detail::error(image::ErrorCode::codec_failure,error.what());}
}
