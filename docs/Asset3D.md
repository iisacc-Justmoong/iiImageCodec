# 3D 자산 입출력

`Asset3D`는 Assimp 6.0.5를 사용하는 선택 모듈이다. 공개 타입은 `Geometry`의 소유 값 타입이다. glTF 2/GLB, OBJ/MTL, STL, PLY, COLLADA, FBX, 3DS 입력과 glTF 2/GLB·OBJ·STL·PLY·COLLADA·FBX 출력을 제공한다. FBX·3DS 등 원본 포맷 전체 기능 보존을 의미하지 않는다.

범위는 정적 삼각형 표면·노드 변환·메시 인스턴스·법선·UV·기본 색·metallic/roughness·기본 색 텍스처이다. 스킨·애니메이션·모프 타깃을 포함한 입력은 정적 데이터로 축소하지 않고 오류를 반환한다. 카메라·조명·정점 색·여러 UV 세트·사용자 정의 속성·특수 셰이더는 이 공통 계약에 포함되지 않는다.

`decode(Bundle)`은 주 파일과 모든 부속 파일을 소유하는 가상 파일 시스템만 읽는다. 호스트 파일 시스템·절대 경로·URL·상위 경로 이동에 접근하지 않는다. OBJ의 MTL·텍스처와 glTF 외부 버퍼도 Bundle에 넣는다. 단일 바이트 입력 오버로드는 외부 파일이 없는 GLB·STL 등에 적합하다. 실제 재질이 참조하는 텍스처가 빠지면 오류이다.

입력 좌표는 `ImportOptions.metres_per_unit`과 `up_axis`로 해석한다. 불명확한 OBJ/STL의 단위를 추정하지 않는다. 내보내기는 미터·Y 위쪽으로 정규화한다. STL/PLY는 계층과 인스턴스를 굽고 포맷이 표현하지 못하는 재질은 생략한다. 정적 표면용 lossy 변환 정책이다. 텍스처 출력은 glTF/GLB에 한정하며 다른 포맷은 명시적으로 거부한다.

`encode`는 파일을 직접 발행하지 않는다. 주 파일명과 함께 OBJ의 MTL·glTF의 BIN 같은 모든 출력 부속 파일을 반환한다. 소비자는 Bundle 전체를 iiFileProvider 등의 경로로 발행한다. 단일 출력 파일의 바이트만 선택하면 부속 파일이 유실될 수 있다.

입력 Bundle 전체 바이트와 파일 수를 파싱 전에 검사하고, 파싱 결과의 정점·인덱스·노드·깊이·텍스처 바이트 및 출력 스트림 전체 바이트를 제한한다. 백엔드 파서의 모든 임시 메모리와 CPU 사용량을 차단하는 격리 기능은 아니다. 네트워크에서 획득한 비신뢰 파일의 강한 격리는 소비자가 담당한다.

```cmake
find_package(iiVisualCodec 0.4 CONFIG REQUIRED COMPONENTS Asset3D)
target_link_libraries(app PRIVATE iiVisualCodec::Asset3D)
```

Assimp는 BSD 라이선스이며 지정된 importer/exporter만 빌드한다. Draco와 도구·샘플·상류 테스트 자산은 패키지에 넣지 않는다. [공식 소스 및 라이선스](https://github.com/assimp/assimp/tree/v6.0.5)를 기준으로 한다.
