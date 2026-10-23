# iiImageCodec

iiImageCodec is the Qt-free, C++23 in-memory image codec module of iiSDK.
It accepts standard byte spans and returns owned RGBA8 pixels or encoded bytes.
It has no application, document, GUI, file system, scheduler or network dependency.
File reads and atomic publication remain the responsibility of iiFileProvider.

```cmake
find_package(iiImageCodec 0.1 CONFIG REQUIRED)
target_link_libraries(my_backend PRIVATE iiImageCodec::iiImageCodec)
```

```cpp
#include <iiImageCodec/ImageCodec.h>
auto png = ii::image::encode_png({width, height, ii::image::PixelFormat::rgb8, stride, rgb});
if (!png) { /* inspect png.error() */ }
```

It reads PNG, JPEG, BMP, GIF, TGA, composite PSD, WebP and TIFF. It writes PNG,
JPEG, BMP, TGA, WebP and TIFF. ICC profiles, orientation and print resolution are
preserved by decoding; `to_srgb()` explicitly converts RGB profiles with native
color management. Layered PSD interchange belongs to iiSharedCanvas.

Build in `build/` with libpng >= 1.6, libjpeg, zlib, WebP >= 1 and TIFF >= 4.5.
Linux color conversion additionally requires Little CMS 2:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/build/install"
```

PNG supports RGB/RGBA input, row padding, alpha and lossless RGBA8 round trips.
JPEG encoding accepts RGB and quality 1..100; decoding accepts grayscale and RGB
JPEG. Unsupported CMYK/YCCK JPEG fails explicitly. All decoding returns RGBA8.
Inspect reads format/dimensions, not an integrity certificate for the full file.

See [contracts and dependency policy](docs/Contracts.md) for limits and exclusions.
