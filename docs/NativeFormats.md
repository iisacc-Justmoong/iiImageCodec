# 네이티브 이미지 형식

Qt 없이 메모리에서 PNG/JPEG/BMP/GIF/TGA/PSD/WebP/TIFF를 읽는다. PNG/JPEG/BMP/TGA/WebP/TIFF를 쓴다. GIF는 첫 이미지, PSD는 합성 이미지를 읽는 API이다. 계층·벡터 PSD는 iiSharedCanvas의 문서 코덱이 소유하며 여기서 합성 이미지로 대체하지 않는다. HDR·다중 페이지 편집·애니메이션 편집은 이 API의 계약이 아니다.

입력은 straight RGBA8로 정규화하며 RGB 값은 원본 프로필의 작업 공간을 유지한다. sRGB 작업 공간이 필요한 소비자는 명시적으로 `to_srgb`를 호출한다. PNG/BMP/TGA/WebP/TIFF 왕복은 투명도를 유지한다. WebP는 RGB와 투명 픽셀까지 보존하는 lossless 설정을 사용한다. JPEG의 일반 `encode`는 명시한 matte 색으로 알파를 합성한다. 기존 `encode_jpeg`는 RGB 입력만 받는 엄격한 계약을 유지한다.

JPEG/PNG/WebP의 EXIF 방향은 픽셀에 적용한 뒤 orientation 1로 정규화한다. PNG/JPEG/WebP/TIFF의 ICC 바이트와 지원하는 해상도 필드를 보존한다. ICC는 최대 1 MiB의 불투명 메타데이터이며 범용 ICC 색 변환을 수행한다는 뜻은 아니다. 인코딩에는 정규화된 방향 1을 요구한다. TIFF는 8-bit RGB/RGBA를 직접 처리하여 straight alpha를 유지한다.

모든 코덱은 입력·출력 바이트, 최대 변과 디코딩 메모리 예산을 검사한다. TIFF의 내부 메모리 할당도 제한한다. 기본 최대 변은 32,768이며 총 픽셀 메모리 기본 제한이 우선한다. 실제 편집 캔버스는 하나의 거대한 GPU 이미지로 올리지 않고 타일을 사용한다.

종속성 키트는 iiSDKBuilder의 `tools/build_native_kit.py`로 버전·체크섬·지원 OS를 고정한다. 재배포 시 STB와 각 이미지 코덱의 라이선스를 함께 배치한다. `iiVisualCodec.contracts`는 실제 각 형식·알파·방향 8종·ICC 왕복, 손상/잘림/예산 입력을 검사한다.
