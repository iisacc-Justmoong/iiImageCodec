"""Independent format checks using host tools; binaries are never linked into the SDK."""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", default=shutil.which("ffmpeg"))
    parser.add_argument("--ffprobe", default=shutil.which("ffprobe"))
    parser.add_argument("--usdcat", default=shutil.which("usdcat"))
    args = parser.parse_args()
    if not all((args.ffmpeg, args.ffprobe, args.usdcat)):
        parser.error("ffmpeg, ffprobe and usdcat are required for this independent check")
    root = ROOT / "build/interoperability"
    root.mkdir(parents=True, exist_ok=True)
    results = []
    def run(command):
        command = [str(x) for x in command]
        process = subprocess.run(command, capture_output=True, text=True, timeout=60)
        results.append(dict(command=command, exit_code=process.returncode, stdout=process.stdout, stderr=process.stderr))
        (root / "report.json").write_text(json.dumps(results, indent=2)+"\n")
        if process.returncode:
            raise RuntimeError(process.stderr or process.stdout)
        return process.stdout
    fixtures = []
    for filename, codec, extra in [("h264.mp4", "libx264", []),
                                   ("hevc.mp4", "libx265", ["-x265-params", "log-level=error:pools=1"]),
                                   ("vp9.webm", "libvpx-vp9", [])]:
        fixture = root / filename
        run([args.ffmpeg, "-v", "error", "-f", "lavfi", "-i", "color=c=red:s=16x16:r=30", "-frames:v", "3",
             "-c:v", codec, "-threads", "1", "-pix_fmt", "yuv420p", *extra, "-y", fixture])
        fixtures.append(fixture)
    run([ROOT / "build/iiVisualCodec_video_interop", *fixtures])
    for name, codec in [("lossless.mkv", "ffv1"), ("movie.mp4", "mpeg4"),
                        ("movie.mov", "mjpeg"), ("movie.avi", "mjpeg"), ("stereo360.mkv", "ffv1"), ("stereo360.mp4", "mpeg4"), ("stereo360-h264.mp4", "h264")]:
        data = json.loads(run([args.ffprobe, "-v", "error", "-count_frames", "-show_entries",
                              "stream=codec_name,width,height,nb_read_frames,time_base:stream_side_data", "-of", "json",
                              ROOT / "build/video-contracts" / name]))
        stream = data["streams"][0]
        assert stream["codec_name"] == codec and stream["nb_read_frames"] == "3", stream
        if name.startswith("stereo360"):
            side = stream["side_data_list"]
            assert any(row.get("projection") == "equirectangular" for row in side)
            assert any(row.get("type") == "side by side" for row in side)
    for name in ("scene.usda", "scene.usdc", "scene.usdz", "textured.usdz"):
        output = root / (name+".usda")
        run([args.usdcat, ROOT / "build/usd-contracts" / name, "-o", output])
        assert "Mesh" in output.read_text(), name
    with zipfile.ZipFile(ROOT / "build/usd-contracts/textured.usdz") as archive:
        names = archive.namelist()
        assert names[0].endswith(".usdc") and any(name.endswith("texture.png") for name in names), names
        # USDZ stores uncompressed ZIP entries with 64-byte-aligned payloads.
        for entry in archive.infolist():
            assert entry.compress_type == zipfile.ZIP_STORED
            with (ROOT / "build/usd-contracts/textured.usdz").open("rb") as file:
                file.seek(entry.header_offset+26)
                name_length, extra_length = struct.unpack("<HH",file.read(4))
                assert (entry.header_offset+30+name_length+extra_length)%64==0
            with archive.open(entry) as data:
                assert data.read(1)
    print("Independent H.264/HEVC/VP9 decode, container metadata and Apple USD checks passed")

if __name__ == "__main__":
    main()
