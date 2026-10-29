# 비주얼 코덱 계약

0.3의 Vision은 Image의 표준 값 타입을 사용하고 Object·Tensor·InferenceModel·Preprocessor·Detector 계약을 제공한다. OpenCV는 Vision 인터페이스를 구현하는 독립 백엔드이다. 원본 좌표·정규화·YOLO 출력·NMS는 VisionAI.md, 네이티브 Mat 경계·영상 처리·ONNX 실행은 OpenCV.md를 따른다. Image/Vision만 패키지로 요청하면 OpenCV를 검색하지 않는다.

`iiVisualCodec 0.4`는 `iiImageCodec`의 이미지 기능을 유지하면서 비디오·VR·AR·USD·Vision·OpenCV·3D·CAD를 제공하는 C++23 모듈이다. 일반 API는 표준 타입을 사용하고 OpenCVBridge.h의 명시적 네이티브 경계에서 cv::Mat을 허용한다. 의존성은 Image → Spatial → Video/USD, Image → Vision → OpenCV, Image → Geometry → Asset3D/CAD/USD 방향이다. 상위 SDK 문서·GUI·스케줄러·네트워크·Qt를 참조하지 않는다. FFmpeg/OpenUSD/Assimp/OCCT 구현 타입은 공개 헤더에 노출하지 않는다.

Geometry의 소유 메시·재질·노드와 Asset3D의 가상 파일 Bundle 및 CAD의 불변 Shape는 각각 Geometry.md·Asset3D.md·CAD.md를 따른다. USD의 `from_geometry`는 미터/Y 위쪽으로 굽고 새 스테이지를 만든다. 기존 USD 문서의 편집 가능한 계층 보존 API와 구분한다.

## 이미지

기본 입력·출력은 각각 64 MiB, 디코딩 픽셀은 256 MiB, 한 변은 32,768이다. 크기·stride의 범위와 곱셈 오버플로를 검사한다. 반환한 바이트·RGBA8 픽셀은 결과가 소유한다. 픽셀은 위에서 아래 순서의 straight alpha이며 ICC 변환은 `to_srgb()`로 명시한다.

PNG/JPEG/BMP/GIF/TGA/composite PSD/WebP/TIFF를 읽고 PNG/JPEG/BMP/TGA/WebP/TIFF를 쓴다. PNG/JPEG/WebP/TIFF의 ICC 프로필, 해상도와 EXIF 방향을 처리하며 디코딩 결과 방향은 정규화된다. `encode_jpeg()`는 RGB만 받고, 범용 `encode(..., Format::jpeg)`는 지정 matte에 RGBA를 합성한다. GIF/PSD는 읽기 전용이며 레이어·애니메이션 문서 보존은 해당 문서 SDK의 책임이다. JPEG의 longjmp는 별도 C 파일 안에서 처리한다.

이미지 형식·색 관리·썸네일의 세부 규칙은 기존 NativeFormats.md, ColorManagement.md, Thumbnails.md를 따른다. `inspect()`는 형식·치수 조사이며 파일 전체의 무결성 보증이 아니다.

## 확장 기능과 소유권

비디오는 동기 메모리 API이다. 컨테이너에서 실제 비디오 스트림을 선택하고 RGBA 프레임·유리수 시간 기준·PTS·duration을 반환한다. 부호 없는 바이트 한도, 디코딩 프레임 수, 전체 RGBA 바이트와 패킷 수를 별도로 검사한다. 외부 URL·파일·재생목록 열기는 차단한다. 오디오·자막 보존은 이 비디오 프레임 API에 포함하지 않는다. 자세한 규칙은 Video.md에 있다.

Spatial은 스테레오 영상, 파노라마 방향, 카메라 내부 파라미터와 깊이 단위를 처리한다. VR 기기의 표시·추적 세션이나 AR 환경 추적은 플랫폼 런타임의 책임이며, 이 모듈은 입력과 결과의 공간 표현·변환을 제공한다. 구체적인 좌표계는 Spatial.md에 있다.

USD는 공식 OpenUSD를 사용하는 독립 백엔드이다. 원본 루트 레이어의 미지 스키마·계층·변형·시간 샘플을 보존하고, 편의 API로 메시·카메라·재질·점군을 편집한다. 파일 기반 USD 입력은 네이티브 resolver로 로컬 에셋을 찾으므로 신뢰할 수 있는 소스에 사용한다. root 파일 바이트 한도와 조회 결과 한도는 전체 종속 에셋을 합산하는 보안 샌드박스나 모든 내부 할당을 차단하는 메모리 할당기가 아니다. USD.md를 참조한다.

모든 Result 오류에는 코드와 원인이 있다. 지원하지 않는 형식·백엔드 기능은 명시적으로 실패한다. 호출 중 입력 수명, 동일 USD Document의 동시 접근, 파일 저장·교체·공개 정책은 호출자가 소유한다. 테스트·빌드가 실행된 플랫폼만 실행 검증으로 보고한다.
