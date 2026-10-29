# VR·AR 공간 영상 처리

`Spatial.h`와 `iiVisualCodec::Spatial`은 플랫폼 장치 세션과 독립적인 영상·공간 변환을 제공한다. 카메라 좌표는 오른손계이며 +X는 오른쪽, +Y는 위, 시선은 -Z이다. Pose.translation은 미터, Pose.rotation은 x/y/z/w 순서의 quaternion이다. 회전 계산은 유한하고 영이 아닌 quaternion을 정규화하여 사용한다.

`pack_stereo()`는 동일 치수의 두 영상을 side_by_side 또는 top_bottom으로 묶고 `extract_eye()`는 지정 눈을 RGBA로 분리한다. 첫 영역이 왼쪽 눈이다. 패킹 축의 홀수 치수·잘못된 stride·초과 픽셀 예산은 거절한다.

`render_view()`는 눈마다 가로:세로 2:1의 360×180도 equirectangular 영상을 원근 시점으로 렌더링한다. 원본 pose와 시점 orientation을 적용하고, 지정 눈 안에서 수평 seam을 순환하며 수직 극점은 clamp한다. 알파를 premultiply하여 보간한 뒤 straight alpha로 반환한다. 수평 시야각은 0도 초과·180도 미만이다. 이 변환에서 위치 이동은 적용하지 않는다. 단일 파노라마에는 병진에 필요한 장면 깊이 정보가 없기 때문이다.

`unproject_depth()`는 양수 유한 깊이를 카메라 -Z축까지의 거리로 해석한다. 내부 파라미터 fx/fy/cx/cy는 픽셀 중심 좌표이고 정수 x/y는 각 픽셀 중심이다. 카메라 점은 `((x-cx)*depth/fx, (cy-y)*depth/fy, -depth)`이며 pose의 회전·이동을 적용하여 월드 미터로 반환한다. 0·음수·NaN·무한 깊이는 결과에서 생략한다. 깊이 stride는 float 원소 수이다. 출력 점군 최악 크기를 pixel_bytes에 대해 먼저 검사한다.

AR 카메라·깊이 센서의 시간 동기화, 렌즈 왜곡 보정, SLAM, 앵커 추적과 VR 헤드셋 표시·입력은 장치 런타임의 역할이다. 여기서 얻은 월드 점군·카메라·영상은 USD Document의 Points/Camera와 비디오 Frame으로 전달할 수 있다. 추적이나 장치 런타임을 실행 검증한 것으로 해석하지 않는다.
