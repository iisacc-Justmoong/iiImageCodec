# CAD 정밀 형상

`CAD`는 Open CASCADE Technology 8.0.1의 정밀 BRep 커널을 사용하는 선택 모듈이다. 공개 API는 표준 C++ 타입과 불투명한 소유 `Shape`이다. Qt와 OpenGL 렌더러를 빌드하지 않는다. 8.0의 공식 STEP/IGES 툴킷이 요구하는 TKDE·XCAF·TKV3d 등의 종속 툴킷은 함께 빌드한다. 이 모듈은 뷰어를 생성하거나 네이티브 CAD 문서 객체를 공개 API에 노출하지 않는다.

상자·원기둥·구·단순 폐합 2D 다각형의 직선 돌출을 생성한다. 합집합·차집합·교집합, 비파괴 아핀 변환, 토폴로지 유효성·정점/모서리/면/셸/솔리드 수·면적·체적·무게중심·경계를 제공한다. 돌출 다각형은 끝점을 중복하지 않으며 최대 4,096점이다. 자기 교차·자기 접촉을 거부한다. 길이는 커널 허용오차보다 큰 `1e-7` mm 초과이며 최대 `1e12` mm이다.

모든 CAD 좌표와 길이는 mm, 면적은 mm², 체적은 mm³이다. STEP의 파일 단위와 IGES의 단위를 mm로 정규화한다. BREP 스트림은 단위를 담지 않으므로 mm 계약으로 읽고 쓴다. STEP/BREP는 메모리 스트림으로 읽고 쓴다. OCCT 8.0.1의 IGES 리더는 스트림을 지원하지 않으므로 IGES 입력은 명시적인 `Shape::read(path, Format::iges)`를 사용한다. IGES 출력은 메모리 바이트이다. 파일 입력은 신뢰할 수 있는 로컬 파일에 사용하고 루트 파일 크기를 먼저 검사한다. 임시 파일을 자동으로 생성하지 않는다. `Shape` 복사는 불변 커널 형상을 공유한다. Boolean·변환은 새 형상을 반환하고, 메시 생성은 커널 복제본에서 수행한다.

STEP/IGES는 형상 교환이다. 어셈블리 제품 트리·부품명·색·레이어·PMI·설계 이력의 편집 가능한 CAD 문서 보존을 주장하지 않는다. IGES는 열린 표면/셸이 될 수 있으며 솔리드가 없는 형상의 체적은 0이다. STEP/IGES 교환은 모듈 내부에서 직렬화한다. 외부 코드가 OCCT 전역 상태를 직접 바꾸며 동시 실행하면 별도 동기화가 필요하다.

`tessellate`는 절대 선형 편차(mm)와 각도 편차(radian)를 사용한다. 결과 메시의 좌표는 mm이며 삼각형마다 CAD 면 식별자를 제공한다. `Geometry::Scene`에 넣을 때 `metres_per_unit=.001`과 원하는 CAD up 축을 지정한다. 이 장면은 `Asset3D`로 GLB·STL 등에 내보내거나 `usd::from_geometry`로 USD 문서에 변환할 수 있다. `source_faces`는 해당 tessellation 안에서만 유효하며 STEP 재저장 후 영속 ID가 아니다.

`Limits`는 입출력 바이트·토폴로지 수·메시 결과를 검사한다. 정확한 곡면의 파싱·Boolean·테셀레이션 내부 할당이나 실행 시간을 모두 제한하지는 않는다. 장시간 연산 취소·프로세스 격리가 필요한 소비자는 별도 실행 경계를 둔다.

```cmake
find_package(iiVisualCodec 0.4 CONFIG REQUIRED COMPONENTS CAD Asset3D)
target_link_libraries(app PRIVATE iiVisualCodec::CAD iiVisualCodec::Asset3D)
```

Open CASCADE는 LGPL 2.1 및 헤더 예외이다. 동적 라이브러리로 빌드하고 원본 소스 archive·빌드 명령·라이선스·예외 원문을 패키지에 동봉한다. 예외를 SDK 전체의 무조건적인 허용 라이선스로 해석하지 않는다. 최종 제품은 라이브러리 교체 가능성·OCCT 사용 고지·해당 라이브러리 소스 제공 등의 배포 조건을 유지해야 한다. iiVisualCodec 자체 라이선스는 MIT이다. 라이브러리와 독립적인 애플리케이션 소스의 공개를 전제로 하는 구현 구조는 아니다. [공식 LGPL 예외 원문](https://github.com/Open-Cascade-SAS/OCCT/blob/V8_0_1/OCCT_LGPL_EXCEPTION.txt)과 [공식 버전](https://github.com/Open-Cascade-SAS/OCCT/releases/tag/V8_0_1)을 참고한다.
