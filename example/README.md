# iiVisualCodec native example

`pipeline.cpp` encodes PNG and FFV1 video, decodes an actual frame, unprojects AR
depth to world points and round-trips those points through an OpenUSD stage.
It is also used by the relocated-package consumer contract.

```sh
cmake -S example -B build/example/build \
  -DCMAKE_PREFIX_PATH="$PWD/build/relocated" -DCMAKE_FIND_FRAMEWORK=LAST
cmake --build build/example/build --parallel 4
ctest --test-dir build/example/build --output-on-failure
```

The prefix must contain the module and its backend dependencies. See
`docs/Packaging.md`; image-only clients request only the Image component.

`vision.cpp` runs native image decoding, OpenCV grayscale conversion, and PNG
encoding. Run `vision_pipeline input.png output.png`. Model execution is supplied
through the independent Vision interfaces; the retired model loader is absent.
