"""Check exported 3D containers independently of Assimp and OCCT using stdlib."""
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def gltf(path):
    if path.suffix == ".glb":
        data = path.read_bytes()
        magic, version, size = struct.unpack_from("<4sII", data)
        assert (magic, version, size) == (b"glTF", 2, len(data))
        chunks, offset = [], 12
        while offset < len(data):
            length, kind = struct.unpack_from("<II", data, offset)
            assert length % 4 == 0 and offset + 8 + length <= len(data)
            chunks.append((kind, data[offset+8:offset+8+length]))
            offset += 8 + length
        assert chunks[0][0] == 0x4E4F534A
        document = json.loads(chunks[0][1])
        buffers = [chunk for kind, chunk in chunks if kind == 0x004E4942]
    else:
        document = json.loads(path.read_text())
        buffers = []
        for buffer in document["buffers"]:
            target = (path.parent / buffer["uri"]).resolve()
            assert path.parent.resolve() in target.parents
            buffers.append(target.read_bytes())
    assert document["asset"]["version"] == "2.0"
    for spec, data in zip(document["buffers"], buffers):
        assert spec["byteLength"] <= len(data)
    def accessor(index):
        spec = document["accessors"][index]
        view = document["bufferViews"][spec["bufferView"]]
        codes = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
        components = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
        fmt = "<" + codes[spec["componentType"]] * components[spec["type"]]
        size = struct.calcsize(fmt)
        stride = view.get("byteStride", size)
        offset = view.get("byteOffset", 0) + spec.get("byteOffset", 0)
        data = buffers[view.get("buffer", 0)]
        assert spec["count"] and offset + (spec["count"]-1)*stride + size <= len(data)
        result = [struct.unpack_from(fmt, data, offset+i*stride) for i in range(spec["count"])]
        assert all(math.isfinite(value) for row in result for value in row)
        return result
    triangles = 0
    mesh_positions, mesh_triangles = [], []
    for mesh in document["meshes"]:
        positions, count = [], 0
        for primitive in mesh["primitives"]:
            assert primitive.get("mode", 4) == 4
            vertices = accessor(primitive["attributes"]["POSITION"])
            indices = accessor(primitive["indices"])
            assert len(indices) % 3 == 0 and all(0 <= row[0] < len(vertices) for row in indices)
            triangles += len(indices)//3
            count += len(indices)//3
            positions.extend(vertices)
        mesh_positions.append(positions)
        mesh_triangles.append(count)
    identity = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
    def multiply(a, b):
        return [sum(a[k*4+r]*b[c*4+k] for k in range(4)) for c in range(4) for r in range(4)]
    def local(node):
        if "matrix" in node:
            return node["matrix"]
        x,y,z,w = node.get("rotation", [0,0,0,1])
        sx,sy,sz = node.get("scale", [1,1,1])
        tx,ty,tz = node.get("translation", [0,0,0])
        return [(1-2*y*y-2*z*z)*sx, (2*x*y+2*z*w)*sx, (2*x*z-2*y*w)*sx, 0,
                (2*x*y-2*z*w)*sy, (1-2*x*x-2*z*z)*sy, (2*y*z+2*x*w)*sy, 0,
                (2*x*z+2*y*w)*sz, (2*y*z-2*x*w)*sz, (1-2*x*x-2*y*y)*sz, 0, tx,ty,tz,1]
    points, instance_triangles = [], 0
    def visit(index, parent):
        nonlocal instance_triangles
        node = document["nodes"][index]
        world = multiply(parent, local(node))
        if "mesh" in node:
            instance_triangles += mesh_triangles[node["mesh"]]
            for p in mesh_positions[node["mesh"]]:
                points.append([sum(world[k*4+r]*p[k] for k in range(3))+world[12+r] for r in range(3)])
        for child in node.get("children", []):
            visit(child, world)
    for index in document["scenes"][document.get("scene", 0)]["nodes"]:
        visit(index, identity)
    assert points
    bounds = [[min(p[k] for p in points) for k in range(3)], [max(p[k] for p in points) for k in range(3)]]
    return dict(meshes=len(document["meshes"]), triangles_in_mesh_pool=triangles,
                nodes=len(document.get("nodes", [])), materials=len(document.get("materials", [])),
                instance_triangles=instance_triangles, world_bounds_m=bounds)

def main():
    root = ROOT / "build/geometry-interoperability"
    root.mkdir(parents=True, exist_ok=True)
    report = {}
    fixtures = ROOT / "build/asset3d-contracts"
    for name in ("scene.glb", "scene.gltf"):
        report[name] = gltf(fixtures / name / name)
        assert report[name]["triangles_in_mesh_pool"] == 1
        assert report[name]["instance_triangles"] == 2
        for actual, expected in zip(sum(report[name]["world_bounds_m"], []), [2,0,-2,3,0,0]):
            assert abs(actual-expected) < 1e-6, (name, actual, expected)
    directory = root / "part"
    if directory.exists():
        # This script owns this exact generated fixture directory only.
        shutil.rmtree(directory)
    process = subprocess.run([str(ROOT / "build/iiVisualCodec_geometry_pipeline"), str(directory)],
                             capture_output=True, text=True, timeout=90)
    assert process.returncode == 0, process.stderr
    report["pipeline"] = process.stdout
    report["part.glb"] = gltf(directory / "part.glb")
    for actual, expected in zip(sum(report["part.glb"]["world_bounds_m"], []), [0,0,-.02,.02,.005,0]):
        assert abs(actual-expected) < 1e-6, (actual, expected)
    data = (directory / "part.stl").read_bytes()
    count = struct.unpack_from("<I", data, 80)[0]
    assert count == report["part.glb"]["triangles_in_mesh_pool"] and len(data) == 84+count*50
    volume = 0
    for offset in range(84, len(data), 50):
        values = struct.unpack_from("<12fH", data, offset)
        assert all(math.isfinite(x) for x in values[:12])
        a, b, c = values[3:6], values[6:9], values[9:12]
        volume += (a[0]*(b[1]*c[2]-b[2]*c[1])+a[1]*(b[2]*c[0]-b[0]*c[2])+a[2]*(b[0]*c[1]-b[1]*c[0]))/6
    exact = (2000-80*math.pi)*1e-9
    assert abs(volume-exact)/exact < 0.01, (volume, exact)
    report["stl_volume_m3"] = volume
    step = (directory / "part.step").read_text()
    assert step.startswith("ISO-10303-21;") and "MANIFOLD_SOLID_BREP" in step and ".MILLI.,.METRE." in step
    usdcat = shutil.which("usdcat")
    if not usdcat:
        raise RuntimeError("Apple/native usdcat is required for this independent check")
    process = subprocess.run([usdcat, str(directory / "part.usda"), "-o", str(root / "part-apple.usda")],
                             capture_output=True, text=True, timeout=60)
    assert process.returncode == 0, process.stderr
    text = (root / "part-apple.usda").read_text()
    assert "Mesh" in text and "metersPerUnit = 1" in text and 'upAxis = "Y"' in text
    report["usdcat"] = dict(executable=usdcat, exit_code=process.returncode)
    (root / "report.json").write_text(json.dumps(report, indent=2)+"\n")
    print("Independent GLB/glTF buffers, STL winding/metric volume, STEP units and Apple USD checks passed")

if __name__ == "__main__":
    main()
