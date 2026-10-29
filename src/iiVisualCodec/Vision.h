#pragma once
#include "ImageCodec.h"
#include <array>
#include <memory>
#include <optional>

namespace ii::visual::vision {
using image::Result;
struct Limits final {
    image::Limits image;
    std::size_t model_bytes = 256 * 1024 * 1024;
    std::size_t tensor_bytes = 256 * 1024 * 1024;
    std::size_t candidates = 10000, objects = 4096, classes = 100000, keypoints = 100000;
};
struct Point final { float x=0, y=0; };
struct Box final { float x=0, y=0, width=0, height=0; }; // source pixels, exclusive right/bottom
struct Keypoint final { Point point; float confidence=0; std::string name; };
struct Mask final { std::uint32_t width=0, height=0; std::vector<std::uint8_t> values; };
struct Object final {
    std::int32_t class_id=-1;
    float confidence=0;
    Box box;
    std::string label;
    std::vector<Keypoint> keypoints;
    std::optional<Mask> mask; // mask grid covers box when attached to an object
    std::optional<std::uint64_t> track_id; // caller-provided identity; no implicit tracker
};
struct Frame final { image::ImageView image; std::int64_t id=0, timestamp_ns=0; };
struct DetectionResult final {
    std::uint32_t width=0, height=0;
    std::int64_t frame_id=0, timestamp_ns=0;
    std::vector<Object> objects;
};
struct Classification final { std::int32_t class_id=-1; float confidence=0; std::string label; };
struct Tensor final {
    std::vector<std::int64_t> shape;
    std::vector<float> values; // owned contiguous, row-major float32
    std::string name;
};
struct FeatureSet final {
    std::vector<Keypoint> keypoints;
    std::size_t descriptor_bytes=0; // bytes per keypoint, row-major binary descriptors
    std::vector<std::uint8_t> descriptors;
};
struct TrackedPoint final { Point point; bool found=false; float error=0; };
enum class ResizeMode { stretch, letterbox };
enum class ChannelOrder { rgb, bgr };
struct PreprocessOptions final {
    std::uint32_t width=640, height=640;
    ResizeMode resize=ResizeMode::letterbox;
    ChannelOrder channels=ChannelOrder::rgb;
    float scale=1.0f/255;
    std::array<float,3> mean{}, standard_deviation{1,1,1};
    std::array<std::uint8_t,3> border{114,114,114}; // RGB
};
struct Transform final {
    std::uint32_t source_width=0, source_height=0, input_width=0, input_height=0;
    float scale_x=1, scale_y=1, pad_x=0, pad_y=0;
};
struct PreparedInput final { Tensor tensor; Transform transform; };
struct DetectionOptions final {
    float score_threshold=0.25f, iou_threshold=0.45f;
    std::size_t max_objects=100;
    bool class_agnostic=false;
};
enum class YoloLayout { channels_first, rows };
enum class BoxEncoding { center_xywh, corners_xyxy };
struct YoloSpec final {
    std::size_t classes=80;
    YoloLayout layout=YoloLayout::channels_first;
    BoxEncoding boxes=BoxEncoding::center_xywh;
    bool objectness=false, normalized_coordinates=false, scores_are_logits=false;
};
[[nodiscard]] Result<void> validate(const Tensor&, Limits = {});
[[nodiscard]] Result<void> validate(const Mask&, Limits = {});
[[nodiscard]] Result<void> validate(const Object&, std::uint32_t width, std::uint32_t height, Limits = {});
[[nodiscard]] Result<void> validate(const PreprocessOptions&, Limits = {});
[[nodiscard]] float intersection_over_union(Box, Box) noexcept;
[[nodiscard]] Result<std::vector<Object>> suppress(std::span<const Object>, DetectionOptions = {}, Limits = {});
[[nodiscard]] Result<std::vector<Object>> decode_yolo(const Tensor&, const Transform&, YoloSpec,
    DetectionOptions = {}, std::span<const std::string> labels = {}, Limits = {});
// A single classification vector, optionally converted from logits by stable softmax.
[[nodiscard]] Result<std::vector<Classification>> classify(const Tensor&, std::size_t top_k=5,
    bool logits=true, std::span<const std::string> labels = {}, Limits = {});

class InferenceModel {
public:
    virtual ~InferenceModel()=default;
    [[nodiscard]] virtual Result<std::vector<Tensor>> run(const Tensor&, Limits = {}) const=0;
};
class Preprocessor {
public:
    virtual ~Preprocessor()=default;
    [[nodiscard]] virtual Result<PreparedInput> prepare(image::ImageView, const PreprocessOptions&, Limits = {}) const=0;
};
class Detector {
public:
    virtual ~Detector()=default;
    [[nodiscard]] virtual Result<DetectionResult> detect(const Frame&, DetectionOptions = {}, Limits = {}) const=0;
};
struct DetectorConfig final {
    PreprocessOptions input;
    YoloSpec output;
    std::string output_name; // empty requires exactly one model output
    std::vector<std::string> labels;
};
class YoloDetector final : public Detector {
public:
    [[nodiscard]] static Result<YoloDetector> create(std::shared_ptr<const InferenceModel>,
        std::shared_ptr<const Preprocessor>, DetectorConfig, Limits = {});
    [[nodiscard]] Result<DetectionResult> detect(const Frame&, DetectionOptions = {}, Limits = {}) const override;
private:
    YoloDetector(std::shared_ptr<const InferenceModel> model, std::shared_ptr<const Preprocessor> preprocessor, DetectorConfig config)
        :model_(std::move(model)), preprocessor_(std::move(preprocessor)), config_(std::move(config)){}
    std::shared_ptr<const InferenceModel> model_;
    std::shared_ptr<const Preprocessor> preprocessor_;
    DetectorConfig config_;
};
}
