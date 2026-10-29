# iiVisualCodec

iiVisualCodec is iiSDK's Qt-free C++23 visual media SDK. It replaces
**iiImageCodec** and provides independent image, video, VR/AR, OpenUSD,
Vision AI, OpenCV, static 3D assets and exact CAD components. Version 0.4 adds
portable geometry, Assimp exchange, Open CASCADE modeling and CAD-to-GLB/USD conversion.
Ordinary public headers use
standard C++ values; `OpenCVBridge.h` is the explicit native `cv::Mat` boundary.

Version 0.4.2 adds cooperative video decode cancellation, reports whether the decoder
had to synthesize a missing timestamp, and declares the exact rate of uniformly timed
encoded frames when the backend can represent it. It also reports decoded frame
display matrices that differ from the stream matrix. Version 0.4.1 added video display inspection for coded dimensions, sample aspect ratio, the
original display matrix and rotation tag, source pixel format, container and
audio stream count. Decoded pixels stay in their coded orientation; the caller
chooses how to apply the presentation transform and handle other streams.

| Component / CMake target | Features |
| --- | --- |
| `iiVisualCodec::Image` | PNG, JPEG, BMP, GIF, TGA, composite PSD, WebP and TIFF decoding; PNG/JPEG/BMP/TGA/WebP/TIFF encoding; ICC conversion and thumbnails |
| `iiVisualCodec::Spatial` | Packed stereo split/join, 360° equirectangular perspective viewports, camera intrinsics, poses and depth-to-world point clouds |
| `iiVisualCodec::Video` | Bounded memory demux/decode/encode, RGBA frames, rational timestamps, MP4/MOV/Matroska/AVI and standard spherical/stereo metadata through FFmpeg |
| `iiVisualCodec::USD` | Native OpenUSD stages, USDA/USDC/USDZ, meshes, cameras, Preview Surface materials and textures, point clouds, transforms and time samples |
| `iiVisualCodec::Vision` | Objects, boxes, keypoints, masks, tensors, model/preprocessor/detector interfaces, YOLO decoding, source coordinates, NMS and classification |
| `iiVisualCodec::OpenCV` | Owned Mat interop, image operations, Canny/regions, ORB, optical flow, annotation and vision preprocessing |
| `iiVisualCodec::Geometry` | Owned triangle meshes, normals, UVs, materials, textures, hierarchy/instances, affine transforms, units, winding and bounds; no 3D backend required |
| `iiVisualCodec::Asset3D` | Memory-only Assimp static asset exchange: glTF/GLB, OBJ/MTL, STL, PLY, COLLADA and FBX; 3DS import |
| `iiVisualCodec::CAD` | Open CASCADE exact shapes, primitives, extrusion, Boolean operations, metrics, tessellation, STEP/BREP memory exchange and explicit IGES file input |
| `iiVisualCodec::iiVisualCodec` | All requested package components |

```cmake
find_package(iiVisualCodec 0.4 CONFIG REQUIRED COMPONENTS Image Spatial Video USD Vision OpenCV Geometry Asset3D CAD)
target_link_libraries(my_backend PRIVATE iiVisualCodec::iiVisualCodec)
# Image-only consumers do not discover FFmpeg, OpenUSD or OpenCV:
# find_package(iiVisualCodec 0.3 CONFIG REQUIRED COMPONENTS Image)
# target_link_libraries(my_backend PRIVATE iiVisualCodec::Image)
```

```cpp
#include <iiVisualCodec/VisualCodec.h>
namespace visual = ii::visual;
auto png = visual::image::encode_png({width, height,
    visual::image::PixelFormat::rgb8, stride, rgb});
visual::video::FrameView frame{rgba_image.view(), 0, 1};
auto movie = visual::video::encode(std::span(&frame, 1)); // lossless FFV1/Matroska

auto scene = visual::usd::Document::create();
// Author meshes/cameras/materials, then export to a new destination:
// scene->write("scene.usdz", visual::usd::Format::usdz);
```

Image and video codecs operate entirely on memory. File reads and atomic
publication belong to the caller (normally iiFileProvider). USD also provides
explicit native file/resolver I/O because scene composition and USDZ packages
can reference local assets. `Document::write` rejects existing destinations.

The default dependency bootstrap pins a GPL/nonfree-disabled **shared LGPL
FFmpeg 8.0.1**, **OpenH264 2.6.0**, **OpenUSD 26.11.1**, **oneTBB 2021.12.0** and
Apache-2.0 **OpenCV 4.14.0**, BSD **Assimp 6.0.5** and shared LGPL-2.1-with-exception
**Open CASCADE 8.0.1**. It verifies source
SHA-256 values and records build commands under `build/dependencies/`. Source
archives and license notices must accompany any redistribution of FFmpeg; the
application's own code remains separate. The module rejects GPL/nonfree FFmpeg
builds at runtime. Image dependencies are libpng >= 1.6, libjpeg, zlib,
WebP >= 1 and TIFF >= 4.5; Linux ICC conversion also needs Little CMS 2.

```sh
python3 tools/bootstrap_dependencies.py --jobs 4
export PKG_CONFIG_PATH="$PWD/build/dependencies/prefix/lib/pkgconfig"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/build/dependencies/prefix" \
  -DCMAKE_FIND_FRAMEWORK=LAST -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/build/install"
```

For image/spatial-only builds, pass `-DIIVISUALCODEC_VIDEO=OFF
-DIIVISUALCODEC_USD=OFF -DIIVISUALCODEC_OPENCV=OFF -DIIVISUALCODEC_3D=OFF
-DIIVISUALCODEC_CAD=OFF`. Native Windows FFmpeg builds require an MSYS2 toolchain;
the C++ SDK also accepts externally built backends through CMake/pkg-config.
`install-native.sh` bootstraps the backends and installs into the shared native
kit under `SDK/build/` by default; `IISDK_NATIVE_PREFIX` changes that destination.
The backend prefix must accompany the installed SDK in the consuming kit.

The legacy `iiImageCodec` CMake package, target, include path and `ii::image`
namespace remain forwarding compatibility surfaces. New code uses
`iiVisualCodec`, `<iiVisualCodec/...>` and `ii::visual::image`.

See [contracts](docs/Contracts.md), [video](docs/Video.md),
[VR/AR processing](docs/Spatial.md), [OpenUSD](docs/USD.md),
[Vision AI](docs/VisionAI.md), [OpenCV](docs/OpenCV.md),
[geometry](docs/Geometry.md), [3D assets](docs/Asset3D.md), [CAD](docs/CAD.md),
[dependency and package delivery](docs/Packaging.md),
[0.4 verification](docs/GeometryVerification.md),
[0.3 verification](docs/VisionVerification.md) and [0.2 verification](docs/Verification.md).
Runnable usage is in `example/`.

`COMPONENTS Geometry` imports no Assimp, OCCT, OpenCV, FFmpeg or USD backend.
`COMPONENTS Asset3D CAD` adds the independent native geometry backends. Asset
decoding uses a bounded virtual file bundle including sidecars; output bundles
must be published in full by the caller. CAD coordinates are millimetres;
3D export normalizes units/up-axis to metres/+Y. `usd::from_geometry` creates a
new baked USD stage. Animation/skins, CAD product metadata and design history
are outside the static interchange contract. See `example/geometry.cpp`.
OCCT source and license/exception notices accompany staged shared libraries;
final product distributions must maintain LGPL replacement and source conditions.

Portable Vision-only clients request `COMPONENTS Vision`. `InferenceModel`,
`Preprocessor`, and `Detector` are independent interfaces for supported native
model implementations. OpenCV provides owned image interop, preprocessing, ORB,
and optical flow. Its former ONNX/DNN model loader has been removed.

`example/vision.cpp` performs an image-to-grayscale pipeline. No model file is
loaded by this example. See [the runtime retirement contract](docs/RuntimeRetirement.md).

## Documentation entry points

- Human overview: this English `README.md`.
- Agent instructions: [AGENT.md](AGENT.md).
- Human documentation: [documentation index](docs/README.md).
- Structured agent documentation: [agent index](docs/agent-index.json).
<!-- workspace-docs:readme:end -->

<!-- workspace-docs:readme:start -->
## Documentation entry points

- Human overview: this English `README.md`.
- Agent instructions: [AGENT.md](AGENT.md).
- Human documentation: [documentation index](docs/README.md).
- Structured agent documentation: [agent index](docs/agent-index.json).
<!-- workspace-docs:readme:end -->
