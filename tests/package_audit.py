"""Audit native macOS package payloads and the actually loaded dependency paths."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
PREFIX = ROOT / "build/relocated"
HEADER_DIRECTORIES = {
    "VisualCodec.h": "Core", "ImageCodec.h": "Image", "Spatial.h": "Spatial",
    "VideoCodec.h": "Video", "UsdCodec.h": "USD", "Vision.h": "Vision",
    "OpenCV.h": "Vision", "OpenCVBridge.h": "Vision", "Geometry.h": "Geometry",
    "Asset3D.h": "Asset3D", "CadCodec.h": "CAD", "UsdGeometry.h": "USD",
}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def archive_objects(path):
    names = subprocess.check_output(["ar", "-t", str(path)], text=True).splitlines()
    return {name: hashlib.sha256(subprocess.check_output(["ar", "-p", str(path), name])).hexdigest()
            for name in names if name.endswith(".o")}

def main():
    if sys.platform != "darwin":
        raise RuntimeError("This runtime-path audit requires native macOS dyld/ar tools")
    report = {}
    for name, path, count in [("release", "build/verification.xml", 14),
                              ("asan_ubsan", "build/asan/build/verification.xml", 14),
                              ("relocated", "build/consumer/build/verification.xml", 8),
                              ("geometry_only", "build/geometry-consumer/build/verification.xml", 2),
                              ("cad_only", "build/cad-consumer/build/verification.xml", 2),
                              ("asset3d_only", "build/asset3d-consumer/build/verification.xml", 2),
                              ("vision_only", "build/vision-consumer/build/verification.xml", 2),
                              ("image_only", "build/image-consumer/build/verification.xml", 1)]:
        xml = ET.parse(ROOT / path).getroot()
        report[name] = dict(xml.attrib)
        assert int(xml.attrib["tests"]) == count
        assert int(xml.attrib.get("failures", 0)) == 0 and int(xml.attrib.get("errors", 0)) == 0
    report["headers"] = {}
    for name, directory in HEADER_DIRECTORIES.items():
        source = ROOT / "src" / directory / name
        staged = PREFIX / "include/iiVisualCodec" / name
        assert digest(source) == digest(staged), name
        report["headers"][name] = digest(staged)
    report["archive_objects"] = {}
    for component in ("Image", "Spatial", "Video", "USD", "Vision", "OpenCV", "Geometry", "Asset3D", "CAD"):
        name = "libiiVisualCodec" + component + ".a"
        source, staged = archive_objects(ROOT / "build" / name), archive_objects(PREFIX / "lib" / name)
        assert source == staged, name
        report["archive_objects"][component] = staged
    report["backend_binaries"] = {}
    for source in (ROOT / "build/dependencies/prefix/lib").glob("*.dylib"):
        assert not any(name in source.name.lower() for name in ("onnx", "opencv_dnn", "libpiper.")), source.name
        if source.is_symlink():
            continue
        staged = PREFIX / "lib" / source.name
        assert digest(source) == digest(staged), source.name
        report["backend_binaries"][source.name] = digest(staged)
    env = dict(os.environ)
    for name in ("DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "DYLD_FALLBACK_LIBRARY_PATH",
                 "DYLD_VERSIONED_LIBRARY_PATH", "LD_LIBRARY_PATH", "PXR_PLUGINPATH_NAME"):
        env.pop(name, None)
    env["DYLD_PRINT_LIBRARIES"] = "1"
    paths = []
    report["example_stdout"] = {}
    directory = ROOT / "build/consumer/build"
    for name, arguments in [("pipeline", []), ("geometry_pipeline", []), ("vision_pipeline", [directory / "vision-contracts/red.png", directory / "vision-contracts/gray.png"])]:
        executable = directory / name
        process = subprocess.run([str(executable), *map(str, arguments)], env=env, capture_output=True, text=True, timeout=180)
        (ROOT / f"build/{name}-libraries.log").write_text(process.stderr)
        assert process.returncode == 0, (process.stdout, process.stderr)
        report["example_stdout"][name] = process.stdout.strip()
        for line in process.stderr.splitlines():
            match = re.search(r"(/[^\n]+)$", line)
            if match and match.group(1).strip() != str(executable):
                paths.append(match.group(1).strip())
    assert paths, "dyld did not provide dependency paths"
    unexpected = [path for path in paths if not path.startswith(
        ("/usr/lib/", "/System/Library/", "/Library/Apple/System/Library/", str(PREFIX) + "/"))]
    assert not unexpected, unexpected
    report["loaded_library_paths"] = paths
    report["qt_or_homebrew_libraries"] = [path for path in paths if "Qt" in path or "/opt/homebrew/" in path]
    assert not report["qt_or_homebrew_libraries"]
    report["pipeline_stdout"] = report["example_stdout"]["pipeline"]
    report["external_interoperability_steps"] = len(json.loads((ROOT / "build/interoperability/report.json").read_text()))
    report["geometry_interoperability"] = json.loads((ROOT / "build/geometry-interoperability/report.json").read_text())
    assert report["geometry_interoperability"]["usdcat"]["exit_code"] == 0
    assert report["geometry_interoperability"]["stl_volume_m3"] > 0
    report["staged_licenses"] = sorted(path.name for path in (PREFIX / "share/licenses").iterdir())
    assert (PREFIX / "share/licenses/OpenH264/LICENSE").is_file()
    assert any((PREFIX / "share/licenses/opencv4").glob("*LICENSE*"))
    assert digest(PREFIX / "share/licenses/OpenCV/LICENSE") == digest(ROOT / "build/dependencies/sources/opencv-4.14.0/LICENSE")
    source_manifest = json.loads((ROOT / "tools/dependencies.json").read_text())
    for item in source_manifest:
        if item["name"] in ("FFmpeg", "OCCT"):
            assert digest(PREFIX / "share/iiVisualCodec/sources" / item["archive"]) == item["sha256"]
    for name, source, notice in [("Assimp", "assimp-6.0.5", "LICENSE"),
                                  ("OCCT", "OCCT-8_0_1", "LICENSE_LGPL_21.txt"),
                                  ("OCCT", "OCCT-8_0_1", "OCCT_LGPL_EXCEPTION.txt")]:
        assert digest(PREFIX / "share/licenses" / name / notice) == digest(ROOT / "build/dependencies/sources" / source / notice)
    for notice in ("rapidjson/license.txt", "pugixml/LICENSE.md", "poly2tri/LICENSE"):
        assert digest(PREFIX / "share/licenses/Assimp/contrib" / notice) == digest(ROOT / "build/dependencies/sources/assimp-6.0.5/contrib" / notice)
    (ROOT / "build/completion-evidence.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Passed: 14 release, 14 ASan/UBSan, 8 relocated; Geometry/CAD/Asset3D/Vision-only 2 each, image-only 1")
    print("Installed headers, archive objects and backend binaries match; loaded dependencies:", len(paths))
    print("Qt/Homebrew runtime libraries:", report["qt_or_homebrew_libraries"])
    print(report["pipeline_stdout"])
    print(report["example_stdout"]["vision_pipeline"])
    print(report["example_stdout"]["geometry_pipeline"])

if __name__ == "__main__":
    main()
