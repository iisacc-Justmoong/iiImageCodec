# 현재 OpenCV 실행 범위

ONNX/DNN 로더는 폐기하였다. 현재 지원은 이미지 처리·전처리·ORB·광류이다.
과거 모델 실행 검증 내용은 아래의 역사적 기록이며 현재 지원 선언이 아니다.

# OpenCV 모듈

`iiVisualCodec::OpenCV`는 Apache 2.0 OpenCV 4.14의 core·imgproc·dnn·features2d·video 모듈을 연결한다. 일반 공개 헤더 OpenCV.h는 표준 C++ 타입만 사용한다. OpenCVBridge.h는 명시적인 네이티브 경계로 cv::Mat 접근을 제공하므로 해당 타깃을 연결한 소비자만 포함한다. Vision 객체만 사용하는 소비자는 이 백엔드를 검색·링크하지 않는다.

## 영상·특징 처리

`to_mat()`/`from_mat()`는 RGB/BGR/RGBA/BGRA/gray 순서를 명시하고 양방향 모두 소유한 버퍼를 반환한다. 이미지 row padding과 연속하지 않은 cv::Mat ROI를 처리한다. Mat 입력은 uint8, 2차원이며 지정 채널 순서와 채널 수가 일치해야 한다. Mat은 ICC·해상도 등의 메타데이터를 전달하지 않는다.

resize()는 nearest/linear/area/cubic을 제공하며 OpenCV의 채널별 보간 규칙을 사용한다. 알파·감마를 위한 별도 색 공간 보간을 자동 수행하지 않는다. grayscale()는 RGBA 결과와 원본 알파를 유지한다. 이 두 이미지 연산은 원본 Metadata를 전달한다. edges()는 Canny 단일 uint8 Mask를 반환한다. regions()는 이진 mask의 연결 영역·면적 필터·bounding box를 계산한다. 이 결과의 class 0/label region은 기하학적 연결 성분이며 학습된 의미 범주가 아니다.

orb_features()는 ORB 키포인트와 이진 descriptor를 반환하고 track_features()는 이전·현재 영상의 Lucas-Kanade 광류로 전달한 점의 새 좌표·status·오차를 반환한다. 광류는 장기 추적 id나 재식별을 자동 제공하지 않는다. draw_objects()는 원본 픽셀 범위의 검출 상자를 새 RGBA 버퍼에 그린다.

## ONNX와 객체 검출

DnnModel::from_onnx()는 호출자가 제공한 메모리 ONNX를 읽는다. 가중치가 포함된 단일 float32 입력 모델을 대상으로 하며 입력 tensor 이름과 모든 출력 tensor 이름을 사용한다. 파일·모델 다운로드·계정·원격 API를 실행하지 않는다. backend는 OpenCV DNN, target은 CPU로 명시한다. GPU·CUDA·NPU 실행을 검증했다고 해석하지 않는다.

run()은 호출자의 입력을 복사하고 반환한 모든 float32 tensor를 소유한다. 모델 복사·공유는 같은 내부 net을 사용하며 mutex가 setInput/forward/출력 복사를 직렬화한다. 독립 모델은 별도 객체로 만들 수 있다. OpenCV의 전역 thread 수나 로그 콜백을 변경하지 않는다. 공개 API의 byte/shape 한도는 OpenCV가 모델을 파싱·실행하며 만드는 전체 중간 버퍼를 제한하는 샌드박스가 아니므로 신뢰할 수 있는 모델을 사용한다.

Vision의 YoloDetector에 DnnModel과 OpenCV Preprocessor를 전달하면 이미지 → letterbox/정규화 → ONNX → YOLO 출력 해석 → 원본 좌표 복원 → NMS → Object로 이어진다. 출력 형태·class 수·input size·라벨은 모델 계약에 맞게 지정해야 한다. 분류 모델은 run()의 출력에 vision::classify()를 적용한다. raw tensor로 다른 검출·키포인트·세그멘테이션 모델의 후처리도 별도로 구성할 수 있다.

## 재현 가능한 의존성·예제

`tools/dependencies.json`은 OpenCV 4.14.0 공식 archive와 SHA-256을 고정한다. `python3 -B tools/bootstrap_dependencies.py --only opencv --jobs 4`로 build/dependencies/에 구성·빌드·설치한다. Qt·highgui·videoio·외부 FFmpeg·CUDA·OpenCL·nonfree를 사용하지 않고, ONNX reader용 protobuf는 archive 안의 소스로 빌드한다. 기존 이미지·비디오 코덱의 책임을 OpenCV로 옮기지 않는다. 설치 prefix의 OpenCV·protobuf 등 재배포 고지도 키트에 함께 복사한다.

전체 SDK는 `IIVISUALCODEC_OPENCV=ON`이며 core만 빌드할 때 `-DIIVISUALCODEC_OPENCV=OFF`를 지정한다. 설치 소비자는 다음과 같이 선택한다.

```cmake
find_package(iiVisualCodec 0.3 CONFIG REQUIRED COMPONENTS Vision OpenCV)
target_link_libraries(app PRIVATE iiVisualCodec::OpenCV)
```

example/vision.cpp는 호출자가 준 ONNX와 이미지를 읽고 class id·confidence·원본 box를 출력한다. 설치 예제를 빌드한 뒤 모델의 실제 계약으로 실행한다.

```sh
vision_pipeline detector.onnx input.png 80 640 640
```

`tests/opencv.cpp`는 Mat/ROI 소유권, 채널 순서, letterbox·정규화, Canny·연결 성분·시각화, 실제 ORB/광류, ONNX 추론·객체·분류, 동일 모델 동시 접근 및 오류 경계를 검사한다. TinyOnnx.h는 RGB 평균에 따라 red/blue 점수가 바뀌는 작은 분석적 ONNX 그래프를 생성한다. 임의로 고정된 결과를 반환하는 가짜 추론은 사용하지 않으며, 이 fixture는 사전학습 객체 검출기의 일반화 정확도를 입증하지 않는다. 테스트가 생성한 detector.onnx/red.png는 build/vision-contracts/에만 저장한다.

공식 API 근거는 [OpenCV DNN 문서](https://docs.opencv.org/4.x/d6/d0f/group__dnn.html), 버전·라이선스 근거는 [4.14.0 공식 배포](https://github.com/opencv/opencv/releases/tag/4.14.0)와 원본 LICENSE이다. 실행 검증 플랫폼은 별도 검증 기록에 명시한다.
