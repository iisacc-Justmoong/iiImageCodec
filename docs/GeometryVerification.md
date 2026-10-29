# iiVisualCodec 0.4 3D·CAD 검증

2026-10-27 macOS ARM64·Apple Clang 21에서 iiVisualCodec 0.4.0을 빌드하고 실행했다. Geometry·Asset3D·CAD를 추가하고 USD에 공통 지오메트리 연결을 제공한다. 구현은 로컬 작업 트리이며 설치 검증은 사용자 설치본 대신 `build/relocated` 키트를 사용한다.

## 실행 결과

| 검증 | 결과 | 증거 |
| --- | --- | --- |
| Release 전체 회귀 | 14/14 통과 | build/verification.xml |
| ASan·UBSan 전체 회귀 | 14/14 통과 | build/asan/build/verification.xml |
| 재배치 설치 키트 전체 소비 | 8/8 통과 | build/consumer/build/verification.xml |
| Geometry 단독 | 2/2 통과 | build/geometry-consumer/build/verification.xml |
| CAD 단독 | 2/2 통과 | build/cad-consumer/build/verification.xml |
| Asset3D 단독 | 2/2 통과 | build/asset3d-consumer/build/verification.xml |
| Vision 단독 | 2/2 통과 | build/vision-consumer/build/verification.xml |
| Image·이전 호환 패키지 | 1/1 통과 | build/image-consumer/build/verification.xml |
| SDK 모듈 등록·경량 소비 플래그 | 5/5 통과 | SDK/tests/test_module_names.py |

Source-only Geometry 8건과 Asset3D 구성 9건도 각각 통과했다. ASan·UBSan은 SDK와 검사 실행 파일의 계측이며, 모든 타사 공유 라이브러리를 계측한 결과는 아니다. Windows·Linux 실제 실행은 수행하지 않았다.

## 검사 범위

Geometry는 유한 좌표·삼각형 인덱스·속성 개수·단위·축·노드 순서·깊이·인스턴스 확장 한도, 역전치 법선과 반사 winding을 검사한다. Asset3D는 GLB·glTF·OBJ·STL 두 종류·PLY 두 종류·COLLADA·FBX의 9개 출력 변형을 실제 왕복한다. 재질·metallic/roughness·투명도·기본 색 텍스처·인스턴스·중첩 가상 경로·부속 파일·바이트 예산을 함께 검사한다. 3DS importer는 빌드에 포함하지만 별도 3DS fixture 실행 결과는 없다.

CAD는 상자·구·원기둥·다각형 돌출·합집합·차집합·교집합을 해석적 체적과 비교한다. 평행 이동은 해석 곡면을 유지하고, 비균등 배율은 일반 BRep 변환을 사용한다. 토폴로지 유효성·면적·경계·부모 형상 불변성, STEP/BREP 메모리 왕복, IGES 로컬 파일 왕복 및 동시 STEP/IGES 교환의 내부 직렬화를 검사한다. 테셀레이션은 바깥쪽 winding·체적·정점 법선·면 ID·결과 한도를 검사한다. IGES 입력은 공식 OCCT의 파일 리더를 사용하며 메모리 디코더로 지원한다고 표시하지 않는다.

`geometry_pipeline`은 20×20×5 mm 블록에 반경 4 mm의 관통 구멍을 뚫는다. 정확한 BRep 체적 `2000−80π mm³`와 비교한 뒤 메시·GLB·USD로 변환한다. GLB를 다시 읽어 미터·Y 위쪽 경계를 확인하고 USD 텍스트를 다시 연다.

`tests/geometry_interoperability.py`는 Assimp/OCCT를 쓰지 않는 별도 Python 표준 라이브러리 파서로 GLB chunk·glTF 버퍼/인덱스·표준 열 우선 노드 행렬·인스턴스 수·세계 경계를 검사한다. binary STL을 직접 읽어 양의 signed volume과 mm→m³ 변환을 확인한다. STEP 파일의 솔리드·mm 단위 선언과 Apple/native `usdcat`의 USD 읽기까지 확인하고 `build/geometry-interoperability/report.json`에 기록한다. 기존 미디어 상호 운용 검사는 `build/interoperability/report.json`에 있다.

최종 패키지 감사 `tests/package_audit.py`는 모든 실행 결과, 공개 헤더 12개·SDK 정적 라이브러리 9개의 오브젝트·백엔드 바이너리 일치, 실제 동적 라이브러리 경로, 라이선스·OCCT/FFmpeg 소스 archive SHA-256을 검사한다. 결과는 `build/completion-evidence.json`, 실제 로드 로그는 `build/*-libraries.log`이다. 타사 헤더를 대량 복사하면서 동시에 회귀를 실행한 초기 검사에서는 기존 USD의 60초 제한에 도달했으며, 복사를 멈추고 동일 코드를 다시 검사하여 전체 회귀가 통과했다. 이 실행 시간은 제품 성능 벤치마크가 아니다.

## 재현

```sh
python3 -B tools/bootstrap_dependencies.py --only 3d --jobs 4
python3 -B tools/bootstrap_dependencies.py --only cad --jobs 8
export PKG_CONFIG_PATH="$PWD/build/dependencies/prefix/lib/pkgconfig"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/build/dependencies/prefix;$PWD/../build/congregation-native/prefix" \
  -DIIVISUALCODEC_3D=ON -DIIVISUALCODEC_CAD=ON -DCMAKE_FIND_FRAMEWORK=LAST
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure --output-junit verification.xml
python3 -B tools/stage_package.py --refresh-geometry --destination build/relocated
cmake --install build --prefix "$PWD/build/relocated"
PKG_CONFIG_PATH="$PWD/build/relocated/lib/pkgconfig" cmake -S tests/consumer -B build/consumer/build \
  -DCMAKE_PREFIX_PATH="$PWD/build/relocated" -DCMAKE_FIND_FRAMEWORK=LAST
cmake --build build/consumer/build --parallel 4
ctest --test-dir build/consumer/build --output-on-failure --output-junit verification.xml
python3 -B tests/geometry_interoperability.py
python3 -B tests/interoperability.py
python3 -B tests/package_audit.py
```

위 refresh는 기존 키트용이다. 새로운 키트는 Packaging.md의 전체 staging 절차를 사용한다. sanitizer 구성은 `build/asan/build`에 `-DIIVISUALCODEC_SANITIZE=ON`을 추가한다. 단독 소비 구성은 tests/consumer의 VISUAL_GEOMETRY_ONLY·VISUAL_CAD_ONLY·VISUAL_3D_ONLY·VISUAL_VISION_ONLY·VISUAL_IMAGE_ONLY 중 하나만 켜고 나머지 백엔드의 CMake 검색을 비활성화한다. OCCT 8.0.1의 패키지 버전 정책에 맞추어 정확한 patch 버전을 요청한다.
