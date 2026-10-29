#!/usr/bin/env python3
"""Stage a relocatable native visual SDK without modifying any user installation."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
IMAGE_HEADERS = ("jconfig.h", "jerror.h", "jmorecfg.h", "jpeglib.h", "png.h", "pngconf.h", "pnglibconf.h",
                 "libpng16", "tiff.h", "tiffconf.h", "tiffio.h", "tiffio.hxx", "tiffvers.h", "webp")
IMAGE_LIBRARIES = ("libjpeg.a", "libpng.a", "libpng16.a", "libtiff.a", "libtiffxx.a", "libwebp.a",
                   "libwebpdecoder.a", "libwebpdemux.a", "libwebpmux.a", "libsharpyuv.a", "libpng", "png.framework")
ASSIMP_NOTICES = ("earcut-hpp/LICENSE", "utf8cpp/doc/LICENSE", "pugixml/LICENSE.md",
                  "rapidjson/license.txt", "openddlparser/LICENSE", "poly2tri/LICENSE",
                  "unzip/unzip.h", "zip/src/zip.h", "zip/src/miniz.h", "stb/stb_image.h")

def copy(source, destination):
    if source.is_symlink():
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists() or destination.is_symlink():
            destination.unlink()
        destination.symlink_to(source.readlink())
    elif source.is_dir():
        destination.mkdir(parents=True, exist_ok=True)
        for child in source.iterdir():
            copy(child, destination / child.name)
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination, follow_symlinks=False)

def geometry_notices(destination):
    source = ROOT / "build/dependencies/sources/assimp-6.0.5/contrib"
    for relative in ASSIMP_NOTICES:
        path = source / relative
        if path.is_file():
            copy(path, destination / "share/licenses/Assimp/contrib" / relative)

def remove_retired_opencv(destination):
    """Remove a stale DNN module when upgrading an owned SDK stage."""
    for path in (destination / "lib").glob("libopencv_dnn*"):
        path.unlink()
    shutil.rmtree(destination / "include/opencv4/opencv2/dnn", ignore_errors=True)
    (destination / "include/opencv4/opencv2/dnn.hpp").unlink(missing_ok=True)

def stage(destination, image_prefix):
    destination = destination.resolve()
    if ROOT / "build" not in destination.parents:
        raise ValueError("The staging destination must be inside this repository's build/")
    if destination.exists():
        raise ValueError("Refusing to replace an existing staging directory")
    backends = ROOT / "build/dependencies/prefix"
    if not (backends / "pxrConfig.cmake").is_file():
        raise ValueError("Run bootstrap_dependencies.py before staging")
    image_prefix = image_prefix.resolve()
    # Check all required input payloads before writing the destination.
    for relative in [*(Path("include")/p for p in IMAGE_HEADERS), *(Path("lib")/p for p in IMAGE_LIBRARIES)]:
        if not (image_prefix / relative).exists():
            raise ValueError(f"Missing image dependency: {image_prefix / relative}")
    shutil.copytree(backends, destination, symlinks=True, ignore=shutil.ignore_patterns("tests"))
    remove_retired_opencv(destination)
    for directory, names in [("include", IMAGE_HEADERS), ("lib", IMAGE_LIBRARIES)]:
        for name in names:
            copy(image_prefix / directory / name, destination / directory / name)
    for name in ("PNG", "libjpeg-turbo", "tiff"):
        copy(image_prefix / "lib/cmake" / name, destination / "lib/cmake" / name)
    copy(image_prefix / "share/WebP", destination / "share/WebP")
    for name in ("jpeg", "png", "tiff", "webp"):
        copy(image_prefix / "share/licenses" / name, destination / "share/licenses" / name)
    subprocess.run(["cmake", "--install", str(ROOT / "build"), "--prefix", str(destination)], check=True)
    provenance = destination / "share/iiVisualCodec/dependencies"
    provenance.mkdir(parents=True, exist_ok=True)
    for path in (ROOT / "build/dependencies").glob("*-provenance.json"):
        copy(path, provenance / path.name)
    copy(ROOT / "tools/dependencies.json", provenance / "sources.json")
    sources = ROOT / "build/dependencies/sources"
    for name, directory, files in [
        ("FFmpeg", "ffmpeg-8.0.1", ("COPYING.LGPLv2.1", "LICENSE.md")),
        ("OpenH264", "openh264-2.6.0", ("LICENSE",)),
        ("OpenCV", "opencv-4.14.0", ("LICENSE", "COPYRIGHT", "NOTICE")),
        ("Assimp", "assimp-6.0.5", ("LICENSE", "CREDITS")),
        ("OCCT", "OCCT-8_0_1", ("LICENSE_LGPL_21.txt", "OCCT_LGPL_EXCEPTION.txt", "README.md")),
        ("OpenUSD", "OpenUSD-26.11.1", ("LICENSE.txt", "NOTICE.txt")),
        ("oneTBB", "oneTBB-2021.12.0", ("LICENSE.txt",))]:
        for filename in files:
            path = sources / directory / filename
            if path.exists():
                copy(path, destination / "share/licenses" / name / filename)
    # LGPL FFmpeg's exact corresponding source accompanies the binary package.
    for item in json.loads((ROOT / "tools/dependencies.json").read_text()):
        if item["name"] in ("FFmpeg", "OCCT"):
            copy(ROOT / "build/dependencies/downloads" / item["archive"], destination / "share/iiVisualCodec/sources" / item["archive"])
    geometry_notices(destination)
    for pc in (destination / "lib/pkgconfig").glob("*.pc"):
        text = pc.read_text().replace(str(backends), "${prefix}")
        lines = text.splitlines()
        lines = ["prefix=${pcfiledir}/../.." if line.startswith("prefix=") else line for line in lines]
        pc.write_text("\n".join(lines)+"\n")
    print(destination)

def refresh_opencv(destination):
    """Merge only the newly built OpenCV backend into an existing owned stage."""
    destination = destination.resolve()
    if ROOT / "build" not in destination.parents or not (destination / "lib/cmake/iiVisualCodec/iiVisualCodecConfig.cmake").is_file():
        raise ValueError("OpenCV refresh requires an existing iiVisualCodec stage inside build/")
    backends = ROOT / "build/dependencies/prefix"
    required = ("include/opencv4", "lib/cmake/opencv4", "share/licenses/opencv4")
    if not all((backends / name).is_dir() for name in required):
        raise ValueError("Build the pinned OpenCV backend before refreshing a stage")
    remove_retired_opencv(destination)
    for relative in (*required, "lib/pkgconfig/opencv4.pc", "share/opencv4"):
        if (backends / relative).exists():
            copy(backends / relative, destination / relative)
    for path in (backends / "lib").glob("libopencv*"):
        copy(path, destination / "lib" / path.name)
    pc = destination / "lib/pkgconfig/opencv4.pc"
    if pc.exists():
        text = pc.read_text().replace(str(backends), "${prefix}")
        pc.write_text("\n".join("prefix=${pcfiledir}/../.." if line.startswith("prefix=") else line for line in text.splitlines())+"\n")
    provenance = destination / "share/iiVisualCodec/dependencies"
    copy(ROOT / "build/dependencies/OpenCV-provenance.json", provenance / "OpenCV-provenance.json")
    copy(ROOT / "tools/dependencies.json", provenance / "sources.json")
    for filename in ("LICENSE", "COPYRIGHT", "NOTICE"):
        path = ROOT / "build/dependencies/sources/opencv-4.14.0" / filename
        if path.exists():
            copy(path, destination / "share/licenses/OpenCV" / filename)
    print("OpenCV backend refreshed:", destination)

def refresh_geometry(destination):
    """Merge pinned Assimp/OCCT files into an existing owned staging kit."""
    destination = destination.resolve()
    if ROOT / "build" not in destination.parents or not (destination / "lib/cmake/iiVisualCodec/iiVisualCodecConfig.cmake").is_file():
        raise ValueError("Geometry refresh requires an existing iiVisualCodec stage inside build/")
    backends = ROOT / "build/dependencies/prefix"
    required = ("include/assimp", "include/opencascade", "lib/cmake/assimp-6.0", "lib/cmake/opencascade")
    if not all((backends / name).is_dir() for name in required):
        raise ValueError("Build pinned Assimp and OCCT before refreshing a stage")
    for relative in (*required, "lib/pkgconfig/assimp.pc", "share/opencascade"):
        if (backends / relative).exists():
            copy(backends / relative, destination / relative)
    for pattern in ("libassimp*", "libTK*"):
        for path in (backends / "lib").glob(pattern):
            copy(path, destination / "lib" / path.name)
    pc = destination / "lib/pkgconfig/assimp.pc"
    if pc.exists():
        text = pc.read_text().replace(str(backends), "${prefix}")
        pc.write_text("\n".join("prefix=${pcfiledir}/../.." if line.startswith("prefix=") else line for line in text.splitlines())+"\n")
    provenance = destination / "share/iiVisualCodec/dependencies"
    for name, source, notices in [
        ("Assimp", "assimp-6.0.5", ("LICENSE", "CREDITS")),
        ("OCCT", "OCCT-8_0_1", ("LICENSE_LGPL_21.txt", "OCCT_LGPL_EXCEPTION.txt", "README.md"))]:
        copy(ROOT / f"build/dependencies/{name}-provenance.json", provenance / f"{name}-provenance.json")
        for notice in notices:
            copy(ROOT / "build/dependencies/sources" / source / notice, destination / "share/licenses" / name / notice)
    copy(ROOT / "tools/dependencies.json", provenance / "sources.json")
    occt = next(row for row in json.loads((ROOT / "tools/dependencies.json").read_text()) if row["name"] == "OCCT")
    copy(ROOT / "build/dependencies/downloads" / occt["archive"], destination / "share/iiVisualCodec/sources" / occt["archive"])
    geometry_notices(destination)
    print("Assimp and OCCT backends refreshed:", destination)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, default=ROOT / "build/package")
    parser.add_argument("--image-prefix", type=Path, default=ROOT.parent / "build/congregation-native/prefix")
    parser.add_argument("--refresh-opencv", action="store_true", help="Merge OpenCV into an existing staged package")
    parser.add_argument("--refresh-geometry", action="store_true", help="Merge Assimp and OCCT into an existing staged package")
    args = parser.parse_args()
    if args.refresh_opencv and args.refresh_geometry:
        parser.error("Choose one backend refresh per invocation")
    if args.refresh_geometry:
        refresh_geometry(args.destination)
    elif args.refresh_opencv:
        refresh_opencv(args.destination)
    else:
        stage(args.destination, args.image_prefix)

if __name__ == "__main__":
    main()
