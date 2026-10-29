if(NOT SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(EXISTS "${SOURCE_ROOT}/src/iiVisualCodec" OR EXISTS "${SOURCE_ROOT}/src/iiImageCodec")
    message(FATAL_ERROR "Source responsibilities must not repeat package names")
endif()
foreach(_file Core/VisualCodec.h Core/Visual_p.h Image/ImageCodec.h Image/ImageCodec.cpp
        Spatial/Spatial.h Spatial/Spatial.cpp Video/VideoCodec.h Video/VideoCodec.cpp
        Vision/Vision.h Vision/Vision.cpp Vision/OpenCV.h Vision/OpenCV.cpp
        Geometry/Geometry.h Geometry/Geometry.cpp Asset3D/Asset3D.h Asset3D/Asset3D.cpp
        CAD/CadCodec.h CAD/CadCodec.cpp USD/UsdCodec.h USD/UsdCodec.cpp Compatibility/ImageCodec.h)
    if(NOT EXISTS "${SOURCE_ROOT}/src/${_file}")
        message(FATAL_ERROR "Missing source in its responsibility directory: ${_file}")
    endif()
endforeach()
