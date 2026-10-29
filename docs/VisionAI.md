# Vision AI 객체와 처리 계약

`iiVisualCodec::Vision`과 `Vision.h`는 C++23 표준 타입으로 영상·검출·분류·추론 계약을 정의한다. OpenCV·특정 모델 런타임·Qt 타입을 공개하지 않으며 OpenCV를 검색하지 않고 단독으로 사용할 수 있다. Vision은 Image의 픽셀·오류 타입만 사용한다. 모델 실행은 InferenceModel, 전처리는 Preprocessor, 프레임 검출은 Detector 인터페이스에 의존한다. 상위 AI 서비스·스케줄러나 iiDecision 구현을 하위 모듈에서 참조하지 않는다. 다른 추론 엔진은 같은 InferenceModel 인터페이스를 구현하여 교체할 수 있다.

## 데이터 객체

- Frame은 이미지 view, frame id와 나노초 timestamp를 받는다. 이미지 메모리는 호출 동안 유효해야 한다. DetectionResult는 원본 치수·id·timestamp와 소유한 검출 객체를 반환한다.
- Object는 class_id, 0..1 confidence, 원본 픽셀 경계 상자, label, 키포인트, 선택적 mask 및 track_id를 가진다. track_id는 호출자가 부여하는 식별자이며 자동 객체 추적 상태가 아니다.
- Box의 x/y는 좌상단, width/height는 양수이고 우측·하단은 배타적 경계이다. 검출 출력은 원본 영상 범위로 자른다. Keypoint는 원본 픽셀 좌표·confidence·이름을 제공한다. Object에 부착한 Mask의 격자는 해당 box 영역에 대응한다.
- Mask는 소유한 단일 uint8 평면이다. 세그멘테이션 결과·이진 영역·에지 결과를 보관할 수 있으며 치수와 byte 개수가 일치해야 한다. 세그멘테이션 모델별 출력 해석기를 자동 선택하지 않는다.
- Tensor는 양수 shape, 연속된 row-major float32 값, 선택적 입출력 이름을 가진다. 원소 수·바이트 한도·유한 값을 검사한다. FeatureSet은 키포인트와 행별 이진 descriptor를 소유한다. TrackedPoint는 광류 결과 좌표·발견 여부·오차를 나타낸다.

## 전처리와 추론

PreprocessOptions는 NCHW `[1,3,H,W]`, stretch/letterbox, RGB/BGR 순서와 `(pixel * scale - mean[channel]) / standard_deviation[channel]`을 명시한다. 기본값은 640×640, RGB, 1/255, letterbox의 RGB border 114이다. 모든 크기·정규화 값을 검증한다. 알파는 모델의 세 RGB 입력에서 사용하지 않으며 ICC 변환은 호출자가 image::to_srgb()로 명시한다.

PreparedInput은 소유한 tensor와 실제 반올림된 리사이즈 치수에 따른 scale_x/scale_y, 좌상단 padding을 반환한다. YoloDetector는 준비된 shape·원본 치수·모델 출력 이름을 검사한다. 여러 모델 출력에는 output_name을 지정해야 한다. 프레임·비디오 디코딩과 비동기 실행의 수명·취소·스케줄링은 소비자가 기존 iiSDK 런타임으로 구성한다.

## 객체 검출·분류 후처리

YoloSpec은 `[1,C,N]` channels-first와 `[1,N,C]` rows, center_xywh 또는 corners_xyxy, objectness 유무, 정규화 좌표와 logit 여부를 명시한다. 일반적인 YOLOv8형 출력과 YOLOv5형 objectness 출력을 이 계약으로 해석한다. class 수와 선택한 label 수가 일치해야 한다. 점수는 확률이거나 명시한 sigmoid logit이며, 프레임 좌표는 letterbox의 실제 scale/padding을 역변환하여 복원한다.

suppress()는 class별 NMS가 기본이며 class_agnostic 선택을 제공한다. 점수 내림차순, 동점 입력 순서를 유지하고 임계값 초과 IoU를 억제한다. classify()는 단일 class 벡터를 안정적인 softmax 또는 제공된 확률로 정렬하여 top-k 결과를 반환한다. 임의 모델의 구조·라벨·정확도를 추측하지 않는다.

## 제한과 검증

기본 model/tensor 한도는 각각 256 MiB, 후보 10,000개, 반환 객체 4,096개, class·키포인트 100,000개이다. 기본 반환 요청은 100개이다. NMS는 mask byte와 키포인트 수를 입력 전체에 대해 합산하며, 모든 내부 문자열·백엔드의 임시 할당을 합산하는 메모리 할당기는 아니다.

`tests/vision.cpp`는 IoU, class별/전체 NMS, 식별자 보존, YOLO 두 레이아웃·objectness·정규화 좌표·padding 제거·라벨, 큰 logit softmax, 잘못된 shape·NaN·한도와 잘못된 인터페이스 구성을 검사한다. `tests/opencv.cpp`는 실제 OpenCV의 소유한 이미지, 전처리, 특징점과 광류 규약을 검사한다. 기존 ONNX/DNN 로더와 그 모델 픽스처는 폐기하였다. 모델 실행은 별도로 주입한 `InferenceModel` 구현의 책임이다.
