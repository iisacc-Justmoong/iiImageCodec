# 시각 코덱 소스 책임 분류

공개 헤더와 구현은 `src/Image`, `src/Spatial`, `src/Video`, `src/Vision`, `src/Geometry`, `src/Asset3D`, `src/CAD`, `src/USD`로 나눈다. 공통 내부 정의와 전체 진입 헤더는 `src/Core`, 이전 이미지 패키지의 헤더 전용 별칭은 `src/Compatibility`에 둔다.

빌드 forwarding 헤더와 설치된 공개 헤더는 기존 `<iiVisualCodec/...>` 및 `<iiImageCodec/ImageCodec.h>` 경로와 개별 CMake 구성요소를 유지한다. `tests/source_layout.cmake`가 책임별 배치를, `tests/package_audit.py`가 설치 헤더의 원문 일치를 검사한다.
