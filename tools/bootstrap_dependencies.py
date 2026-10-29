#!/usr/bin/env python3
"""Reproduce the optional native visual backends inside this repository's build/."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = json.loads((ROOT / "tools/dependencies.json").read_text())

def extract(archive, destination):
    # Official archives are still checked before extraction. Reject symlinks,
    # hardlinks and path traversal rather than trusting archive member names.
    with tarfile.open(archive) as tar:
        members = tar.getmembers()
        prefix = Path(members[0].name).parts[0]
        for member in members:
            parts = Path(member.name).parts
            if not parts or parts[0] != prefix or ".." in parts or Path(member.name).is_absolute():
                raise ValueError("unsafe dependency archive path")
            if member.issym() or member.islnk():
                raise ValueError("dependency archive contains a link")
            if not (member.isfile() or member.isdir()):
                raise ValueError("dependency archive contains a special filesystem entry")
        tar.extractall(destination)
    return destination / prefix

def verify_cmake_cache(cache_path, source, prefix, flags):
    if not cache_path.is_file():
        raise ValueError("--resume requires an existing CMake configuration")
    contents = cache_path.read_bytes()
    cache = {}
    for line in contents.decode().splitlines():
        if line and not line.startswith(("//", "#")) and ":" in line and "=" in line:
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    if cache.get("CMAKE_HOME_DIRECTORY") != str(source) or cache.get("CMAKE_INSTALL_PREFIX") != str(prefix):
        raise ValueError("--resume configuration source/prefix does not match")
    for flag in flags:
        if isinstance(flag, str) and flag.startswith("-D"):
            key, value = flag[2:].split("=", 1)
            # Upstream OCCT removes unused third-party switches from its cache.
            if key in cache and cache[key] != value:
                raise ValueError(f"--resume configuration differs: {key}")
    return hashlib.sha256(contents).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--resume", action="store_true", help="Resume a matching existing CMake configuration without repeating header collection")
    parser.add_argument("--only", choices=("video", "usd", "opencv", "3d", "cad", "all"), default="all")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    work = ROOT / "build/dependencies"
    prefix = work / "prefix"
    for directory in (work / "downloads", work / "sources", work / "logs", prefix):
        directory.mkdir(parents=True, exist_ok=True)
    records = []
    selected = {"video": {"OpenH264", "FFmpeg"}, "usd": {"oneTBB", "OpenUSD"},
                "opencv": {"OpenCV"}, "3d": {"Assimp"}, "cad": {"OCCT"},
                "all": {row["name"] for row in MANIFEST}}[args.only]
    if args.only == "all":
        selected.update(("OpenH264", "FFmpeg"))
    for item in MANIFEST:
        if item["name"] not in selected:
            continue
        name = item["name"]
        archive = work / "downloads" / item["archive"]
        if not archive.exists():
            partial = archive.with_suffix(archive.suffix + ".partial")
            urllib.request.urlretrieve(item["url"], partial)
            partial.rename(archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != item["sha256"]:
            raise ValueError(f"{name} archive SHA-256 mismatch")
        source_name = {"FFmpeg": "ffmpeg-8.0.1", "OpenH264": "openh264-2.6.0", "oneTBB": "oneTBB-2021.12.0", "OpenUSD": "OpenUSD-26.11.1", "OpenCV": "opencv-4.14.0", "Assimp": "assimp-6.0.5", "OCCT": "OCCT-8_0_1"}[name]
        source = work / "sources" / source_name
        if not source.exists():
            source = extract(archive, work / "sources")
        binary = work / name / "build"
        binary.mkdir(parents=True, exist_ok=True)
        commands = []
        adjustments = []
        def run(command):
            commands.append([str(x) for x in command])
            print(name, " ".join(commands[-1]), flush=True)
            with (work / "logs" / f"{name}.log").open("a") as log:
                env = dict(os.environ)
                for variable in ("CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH",
                                 "LIBRARY_PATH", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS",
                                 "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH"):
                    env.pop(variable, None)
                temporary = binary / "tmp"
                temporary.mkdir(exist_ok=True)
                env["TMPDIR"] = str(temporary)
                if platform.system() == "Darwin":
                    env["MACOSX_DEPLOYMENT_TARGET"] = "12.0"
                env["PKG_CONFIG_PATH"] = str(prefix / "lib/pkgconfig") + os.pathsep + env.get("PKG_CONFIG_PATH", "")
                subprocess.run(commands[-1], cwd=binary, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        if name == "OpenH264":
            command = ["make", "-f", source / "Makefile", f"PREFIX={prefix}", "BUILDTYPE=Release"]
            if platform.system() == "Darwin":
                command += ["SHAREDLIB_DIR=@rpath"]
            run([*command, f"-j{args.jobs}", "libraries"])
            # Install in prefix/lib while retaining the @rpath install name.
            install = [arg for arg in command if not str(arg).startswith("SHAREDLIB_DIR=")]
            run([*install, "install-shared"])
        elif name == "FFmpeg":
            if os.name == "nt":
                raise RuntimeError("Build FFmpeg with an MSYS2 shell/toolchain; pass its pkg-config prefix to CMake")
            flags = [str(source / "configure"), f"--prefix={prefix}", "--enable-shared", "--disable-static",
                     "--disable-gpl", "--disable-nonfree", "--disable-version3", "--disable-autodetect",
                     "--disable-programs", "--disable-doc", "--disable-network", "--disable-avdevice",
                     "--disable-avfilter", "--disable-swresample", "--disable-debug", "--disable-everything",
                     "--enable-avcodec", "--enable-avformat", "--enable-avutil", "--enable-swscale", "--enable-libopenh264",
                     "--enable-decoder=h264,hevc,vp9,av1,mpeg4,mjpeg,ffv1,rawvideo,png",
                     "--enable-encoder=ffv1,mpeg4,mjpeg,rawvideo,png,libopenh264",
                     "--enable-parser=h264,hevc,vp9,av1,mpeg4video,mjpeg",
                     "--enable-demuxer=matroska,mov,avi", "--enable-muxer=matroska,mp4,mov,avi",
                     "--enable-bsf=av1_frame_merge,extract_extradata,vp9_superframe_split"]
            if platform.system() == "Darwin":
                flags += ["--enable-videotoolbox", "--enable-encoder=h264_videotoolbox,hevc_videotoolbox",
                          "--install-name-dir=@rpath", "--extra-cflags=-mmacosx-version-min=12.0",
                          "--extra-ldflags=-mmacosx-version-min=12.0"]
            run(flags)
            run(["make", f"-j{args.jobs}"])
            run(["make", "install"])
            # pkg-config's prefix/libdir/include paths must follow a relocated
            # SDK kit, rather than pointing back into this checkout's build/.
            for pc in (prefix / "lib/pkgconfig").glob("lib*.pc"):
                text = pc.read_text().replace(str(prefix / "lib"), "${prefix}/lib").replace(str(prefix / "include"), "${prefix}/include")
                text = text.replace(f"prefix={prefix}", "prefix=${pcfiledir}/../..")
                pc.write_text(text)
        else:
            flags = ["cmake", "-S", source, "-B", binary, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                     f"-DCMAKE_INSTALL_PREFIX={prefix}", f"-DCMAKE_PREFIX_PATH={prefix}",
                     "-DCMAKE_INSTALL_LIBDIR=lib", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"]
            if name == "oneTBB":
                flags += ["-DTBB_TEST=OFF", "-DTBB_STRICT=OFF"]
            elif name == "Assimp":
                flags += ["-DBUILD_SHARED_LIBS=ON", "-DASSIMP_BUILD_TESTS=OFF",
                          "-DASSIMP_BUILD_ASSIMP_TOOLS=OFF", "-DASSIMP_BUILD_SAMPLES=OFF",
                          "-DASSIMP_BUILD_ZLIB=OFF", "-DASSIMP_WARNINGS_AS_ERRORS=OFF",
                          "-DASSIMP_BUILD_DRACO=OFF", "-DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF",
                          "-DASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT=OFF"]
                for fmt in ("OBJ", "PLY", "STL", "GLTF", "FBX", "COLLADA", "3DS"):
                    flags += [f"-DASSIMP_BUILD_{fmt}_IMPORTER=ON"]
                for fmt in ("OBJ", "PLY", "STL", "GLTF", "FBX", "COLLADA"):
                    flags += [f"-DASSIMP_BUILD_{fmt}_EXPORTER=ON"]
                if platform.system() == "Darwin":
                    sdk = subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()
                    flags += [f"-DZLIB_INCLUDE_DIR={sdk}/usr/include", f"-DZLIB_LIBRARY={sdk}/usr/lib/libz.tbd"]
            elif name == "OCCT":
                flags += ["-DBUILD_LIBRARY_TYPE=Shared", "-DBUILD_MODULE_FoundationClasses=OFF",
                          "-DBUILD_MODULE_ModelingData=OFF", "-DBUILD_MODULE_ModelingAlgorithms=OFF",
                          "-DBUILD_MODULE_DataExchange=OFF", "-DBUILD_MODULE_ApplicationFramework=OFF",
                          "-DBUILD_ADDITIONAL_TOOLKITS=TKDESTEP;TKDEIGES;TKMesh;TKPrim;TKBO;TKTopAlgo;TKShHealing",
                          "-DBUILD_MODULE_Visualization=OFF", "-DBUILD_MODULE_Draw=OFF",
                          "-DBUILD_SAMPLES_QT=OFF", "-DBUILD_DOC_Overview=OFF", "-DBUILD_USE_PCH=ON",
                          "-DUSE_TCL=OFF", "-DUSE_TK=OFF", "-DUSE_FREETYPE=OFF",
                          "-DUSE_FREEIMAGE=OFF", "-DUSE_FFMPEG=OFF", "-DUSE_TBB=OFF",
                          "-DUSE_VTK=OFF", "-DUSE_OPENGL=OFF", "-DUSE_GLES2=OFF",
                          "-DUSE_RAPIDJSON=OFF", "-DUSE_DRACO=OFF", "-DUSE_EIGEN=OFF",
                          "-DINSTALL_DIR_LAYOUT=Unix", "-DINSTALL_DIR_INCLUDE=include/opencascade",
                          "-DINSTALL_DIR_LIB=lib", "-DINSTALL_DIR_CMAKE=lib/cmake/opencascade"]
            elif name == "OpenCV":
                flags += ["-DBUILD_LIST=core,imgproc,features2d,video", "-DBUILD_SHARED_LIBS=ON", "-DBUILD_opencv_dnn=OFF",
                          "-DBUILD_TESTS=OFF", "-DBUILD_PERF_TESTS=OFF", "-DBUILD_EXAMPLES=OFF",
                          "-DBUILD_opencv_apps=OFF", "-DBUILD_JAVA=OFF", "-DBUILD_opencv_python2=OFF",
                          "-DBUILD_opencv_python3=OFF", "-DWITH_QT=OFF", "-DWITH_GTK=OFF",
                          "-DWITH_FFMPEG=OFF", "-DWITH_GSTREAMER=OFF", "-DWITH_TBB=OFF",
                          "-DWITH_OPENCL=OFF", "-DWITH_IPP=OFF", "-DWITH_ITT=OFF",
                          "-DWITH_LAPACK=OFF", "-DWITH_EIGEN=OFF", "-DWITH_OPENEXR=OFF",
                          "-DWITH_VTK=OFF", "-DWITH_CUDA=OFF", "-DWITH_PROTOBUF=ON",
                          "-DBUILD_PROTOBUF=ON", "-DOPENCV_DNN_OPENCL=OFF", "-DOPENCV_ENABLE_NONFREE=OFF",
                          "-DOPENCV_GENERATE_PKGCONFIG=ON", f"-DOPENCV_DOWNLOAD_PATH={work / 'downloads/OpenCV'}"]
            else:
                flags += ["-DPXR_ENABLE_PYTHON_SUPPORT=OFF", "-DPXR_BUILD_IMAGING=OFF",
                          "-DPXR_BUILD_USD_IMAGING=OFF", "-DPXR_BUILD_TESTS=OFF", "-DPXR_BUILD_EXAMPLES=OFF",
                          "-DPXR_BUILD_TUTORIALS=OFF", "-DPXR_BUILD_USD_TOOLS=OFF", "-DPXR_BUILD_USD_VALIDATION=OFF",
                          "-DPXR_ENABLE_MATERIALX_SUPPORT=OFF", "-DPXR_ENABLE_OPENVDB_SUPPORT=OFF",
                          "-DPXR_BUILD_MONOLITHIC=OFF", "-DPXR_FIND_TBB_IN_CONFIG=ON"]
            if platform.system() == "Darwin":
                flags += ["-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0", "-DCMAKE_INSTALL_NAME_DIR=@rpath"]
            if args.resume:
                cache_path = binary / "CMakeCache.txt"
                adjustments.append("resumed matching CMake cache SHA-256: " + verify_cmake_cache(cache_path, source, prefix, flags))
                print(name, "resuming existing CMake configuration", flush=True)
            else:
                run(flags)
            run(["cmake", "--build", binary, "--parallel", args.jobs])
            run(["cmake", "--install", binary])
            if name == "OpenCV":
                # Reconfiguration does not uninstall an old optional module.
                # The SDK no longer ships the retired ONNX/DNN model path.
                for obsolete in (prefix / "lib").glob("libopencv_dnn*"):
                    obsolete.unlink()
                shutil.rmtree(prefix / "include/opencv4/opencv2/dnn", ignore_errors=True)
                (prefix / "include/opencv4/opencv2/dnn.hpp").unlink(missing_ok=True)
            if name == "Assimp" and platform.system() == "Darwin":
                # Assimp exports its private zlib path. Use the platform linker
                # name so a kit does not depend on this machine's Xcode path.
                targets = prefix / "lib/cmake/assimp-6.0/assimpTargets.cmake"
                targets.write_text(targets.read_text().replace(f"{sdk}/usr/lib/libz.tbd", "z"))
                adjustments.append("assimpTargets.cmake: replace build-SDK libz.tbd with system z")
        record = dict(item, commands=commands, package_adjustments=adjustments)
        if name not in ("OpenH264", "FFmpeg"):
            record["cmake_configuration"] = [str(arg) for arg in flags]
        records.append(record)
        # Incremental records survive an interrupted build of a later library.
        (work / f"{name}-provenance.json").write_text(json.dumps(records[-1], indent=2) + "\n")
    print(f"Backends installed under {prefix}", flush=True)

if __name__ == "__main__":
    main()
