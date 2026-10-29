#include <iiVisualCodec/Vision.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace ii::visual;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while(false)
int main(){
    namespace v=vision;
    CHECK(std::abs(v::intersection_over_union({0,0,10,10},{5,0,10,10})-1.0f/3)<1e-6);
    CHECK(v::intersection_over_union({0,0,0,1},{0,0,1,1})==0);
    v::Object first;first.class_id=0;first.confidence=.9f;first.box={0,0,10,10};first.track_id=7;
    auto second=first;second.confidence=.8f;second.box.x=1;
    auto other=second;other.class_id=1;
    std::vector<v::Object> candidates{first,second,other};
    auto nms=v::suppress(candidates);CHECK(nms && nms->size()==2 && nms->front().track_id==7);
    v::DetectionOptions options;options.class_agnostic=true;nms=v::suppress(candidates,options);CHECK(nms && nms->size()==1);
    options={};options.max_objects=1;CHECK(v::suppress(candidates,options)->size()==1);
    candidates[0].confidence=std::numeric_limits<float>::quiet_NaN();CHECK(!v::suppress(candidates));
    CHECK(v::validate(first,10,10));first.box.x=-1;CHECK(!v::validate(first,10,10));
    v::Tensor tensor{{1,6,2},{50,51,50,50,40,40,20,20,.9f,.8f,.1f,.2f},"objects"};
    // Letterboxed 100x50 source -> 100x100 network. y padding must be removed.
    v::Transform transform{100,50,100,100,1,1,0,25};v::YoloSpec spec;spec.classes=2;
    const std::vector<std::string> labels{"red","blue"};
    auto objects=v::decode_yolo(tensor,transform,spec,{},labels);CHECK(objects && objects->size()==1);
    CHECK(objects->front().box.x==30 && objects->front().box.y==15 && objects->front().label=="red");
    v::Tensor rows{{1,2,7},{.5f,.5f,.4f,.2f,.5f,.9f,.1f, .51f,.5f,.4f,.2f,.5f,.8f,.2f},"objects"};
    spec.layout=v::YoloLayout::rows;spec.objectness=true;spec.normalized_coordinates=true;
    objects=v::decode_yolo(rows,transform,spec,{},labels);CHECK(objects && objects->size()==1 && std::abs(objects->front().confidence-.45f)<1e-6);
    auto malformed=rows;malformed.values.pop_back();CHECK(!v::decode_yolo(malformed,transform,spec));
    transform.scale_x=0;CHECK(!v::decode_yolo(rows,transform,spec));
    v::Tensor logits{{1,3},{1000,1001,999},"classes"};auto classes=v::classify(logits,2);CHECK(classes && classes->size()==2 && classes->front().class_id==1);
    CHECK(std::abs(classes->front().confidence-.66524f)<.0001f);
    v::Limits limit;limit.tensor_bytes=8;CHECK(!v::validate(logits,limit));
    CHECK(!v::validate(v::Tensor{{std::numeric_limits<std::int64_t>::max(),3},{1},"bad"}));
    CHECK(!v::validate(v::Mask{2,2,{1,2,3}}));
    v::PreprocessOptions preprocessing;preprocessing.standard_deviation[0]=0;CHECK(!v::validate(preprocessing));
    CHECK(!v::YoloDetector::create(nullptr,nullptr,{}));
    std::cout<<"vision contracts passed: object geometry, NMS, YOLO layouts, source coordinates, classification and limits\n";
}
