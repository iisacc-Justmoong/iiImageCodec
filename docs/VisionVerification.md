# 과거 모델 검증 기록

2026-10-27에 ONNX 실행 경로를 폐기하였다. 아래 내용은 과거 증거이며 현재
모델 실행 지원이나 현재 빌드 성공을 의미하지 않는다.

# iiVisualCodec 0.3 Vision·OpenCV 검증

이 문서는 0.3 당시의 검증 기록이다. 현재 0.4 코드와 build/ 증거의 기준은 GeometryVerification.md이며 같은 build/ 경로의 결과 파일은 최신 실행으로 갱신된다.

2026-10-27 macOS ARM64·Apple Clang 21에서 소스·설치 소비자를 검증했다. 0.3.0은 기존 Image·Video·Spatial·USD에 Vision·OpenCV 타깃을 추가한 버전이다. 공개 표준 타입·교체 가능한 모델/전처리/검출 인터페이스와 OpenCV 네이티브 경계를 구분한다. 기존 사용자 설치본은 변경하지 않고 build/relocated 키트에서 검증했다. 구현 변경은 로컬 작업 트리에 있다.

## 결과와 증거

| 검증 | 결과 | 기록 |
| --- | --- | --- |
| Release 전체 회귀 | 10/10 통과 | build/verification.xml |
| ASan·UBSan 전체 회귀 | 10/10 통과 | build/asan/build/verification.xml |
| 재배치 전체 소비자·OpenCV 계약·Vision CLI | 4/4 통과 | build/consumer/build/verification.xml |
| OpenCV·FFmpeg·USD 검색을 끈 Vision 전용 소비자 | 2/2 통과 | build/vision-consumer/build/verification.xml |
| OpenCV 검색을 끈 이미지 전용 소비자 | 1/1 통과 | build/image-consumer/build/verification.xml |
| SDK 명칭과 이미지 전용 소스 설정 | 5/5 통과 | SDK/tests/test_module_names.py |
| 설치 헤더·archive 객체·백엔드 바이트·실제 로드 경로 | 통과 | build/completion-evidence.json |
| 기존 비디오·USD 외부 도구 호환성 | 통과 | build/interoperability/report.json |

sanitizer는 iiVisualCodec과 테스트에 적용했다. 사전 빌드한 OpenCV·FFmpeg·OpenUSD 전체를 sanitizer로 다시 빌드한 것은 아니다. 기존 이미지·색 관리·썸네일·비디오·VR/AR·USD·구 API·의존성 계약도 10개 항목에 함께 포함된다.

## 검증한 동작

Vision 테스트는 실제 기대 IoU, class별/전체 NMS, 원본 순서·track id 보존, YOLO channels-first/rows, objectness·정규화 좌표·label·letterbox 역변환, 큰 logit softmax, 잘못된 크기·NaN·한도와 불완전한 detector 구성을 검사했다.

OpenCV 테스트는 소유 버퍼 복사와 비연속 Mat ROI, RGB/BGR 채널 순서, RGBA 알파 보존, 리사이즈·그레이스케일·Canny·연결 영역·상자 그리기를 실행했다. 실제 ORB descriptor를 만들었고, 알려진 2픽셀/1픽셀 이동 영상의 광류 좌표가 기대 범위 안으로 이동하는지 검사했다.

TinyOnnx.h의 embedded-weight ONNX는 RGB 평균을 계산하여 검출/분류 출력을 만든다. 빨간 입력은 class 0, 파란 입력은 class 1로 달라지는 것을 실제 DNN forward와 Object 결과에서 확인했다. frame id/timestamp, 원본 경계 상자, NMS와 label도 함께 확인했다. 동일 모델에 두 동시 작업을 보내 입력·출력 상태가 섞이지 않는지 검사했다. 이 모델은 실행·좌표·호출 계약의 분석적 fixture이며 사전학습 검출기의 일반화 정확도를 측정하는 자료는 아니다.

설치된 Vision/OpenCV 타깃만으로 같은 계약을 다시 컴파일하고 example/vision.cpp를 실행했다. CLI에 detector.onnx·red.png·class 수 2·입력 2×2를 전달한 결과는 객체 1개, class 0, confidence 1, 원본 box 0.5/0.5/1/1이다. 픽스처는 build/ 하위에서만 생성했다. 모델·라벨·입력 크기는 실제 사용 모델에 맞춰 호출자가 제공한다.

## 패키지와 의존성

OpenCV 4.14.0은 공식 archive·SHA-256으로 고정해 core·imgproc·dnn·features2d·video를 빌드했다. Qt·highgui·videoio·외부 FFmpeg·CUDA·OpenCL·nonfree를 사용하지 않는다. OpenCV 및 포함한 protobuf 등의 재배포 고지와 원본 OpenCV LICENSE를 키트에 보존했다. 의존성 recipe·명령·로그는 build/dependencies/와 tools/dependencies.json에 있다.

표준 헤더 8개, SDK 정적 라이브러리 6개는 설치본과 각각 바이트/객체 payload가 일치했다. 백엔드 공유 라이브러리도 원본 dependency prefix와 바이트가 일치했다. DYLD_PRINT_LIBRARIES와 사용자 라이브러리 경로 환경변수를 제거한 두 예제를 실행하여 비시스템 라이브러리가 재배치 키트에서 로드됨을 확인했다. Qt·Homebrew 런타임은 없었다. 로그는 build/pipeline-libraries.log와 build/vision_pipeline-libraries.log이다.

## 재실행

```sh
python3 -B tools/bootstrap_dependencies.py --only opencv --jobs 4
cmake -S . -B build -DIIVISUALCODEC_OPENCV=ON \
  -DCMAKE_PREFIX_PATH="$PWD/build/dependencies/prefix;$PWD/../build/congregation-native/prefix"
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure --output-junit verification.xml
```

전체 의존성·키트 준비는 Packaging.md를 따른다. 처음에는 stage_package.py로 새 키트를 만들고 경로를 옮긴다. 기존 키트에는 --refresh-opencv로 새 OpenCV payload를 합친 뒤 SDK를 설치할 수 있다. 모든 테스트 빌드는 build/ 아래에 둔다.

```sh
python3 -B tools/stage_package.py --refresh-opencv --destination build/relocated
cmake --install build --prefix "$PWD/build/relocated"
cmake -S tests/consumer -B build/consumer/build \
  -DCMAKE_PREFIX_PATH="$PWD/build/relocated" -DCMAKE_FIND_FRAMEWORK=LAST
cmake --build build/consumer/build --parallel 4
ctest --test-dir build/consumer/build --output-on-failure --output-junit verification.xml
cmake -S tests/consumer -B build/vision-consumer/build -DVISUAL_VISION_ONLY=ON \
  -DCMAKE_PREFIX_PATH="$PWD/build/relocated" -DCMAKE_FIND_FRAMEWORK=LAST \
  -DCMAKE_DISABLE_FIND_PACKAGE_OpenCV=ON -DCMAKE_DISABLE_FIND_PACKAGE_pxr=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON
cmake --build build/vision-consumer/build --parallel 4
ctest --test-dir build/vision-consumer/build --output-on-failure --output-junit verification.xml
```

Image-only와 ASan·UBSan 구성도 각 JUnit 기록을 만든 뒤 tests/package_audit.py를 실행한다. PKG_CONFIG_PATH는 현재 사용하는 dependency/relocated prefix를 선택한다. 감사는 현재 10/10/4/2/1 기록, 설치 payload와 두 실제 예제를 함께 검사한다.

검증 범위는 macOS ARM64 CPU ONNX 실행이다. GPU·NPU·Windows·Linux·모바일 실행이나 특정 사전학습 모델의 정확도는 이 기록의 결과가 아니다. 기존 SDK 전체 등록 검사의 iiVirtualizer 누락은 0.2 기록에 별도로 남겨 두었으며 이번 범위의 변경으로 처리하지 않았다.
