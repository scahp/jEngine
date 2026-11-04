#!/usr/bin/env python3
"""Stage a GLB or legacy .scene with a shared jEngine material library."""

import argparse
import json
import re
import shutil
import struct
import tempfile
import uuid
from pathlib import Path
from urllib.parse import unquote


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def copy_dependency(source_root, target_root, relative):
    relative = Path(unquote(relative.replace("\\", "/")))
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError(f"Expected a scene-relative dependency: {relative}")
    source = (source_root / relative).resolve(strict=True)
    if not source.is_relative_to(source_root) or not source.is_file():
        raise ValueError(f"Invalid scene dependency: {relative}")
    target = target_root / relative
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    return relative.as_posix()


def graph(color, metallic, roughness, albedo, normal, surface):
    nodes = []

    def node(kind, *, a=0, b=0, rgb=(1, 1, 1), value=1, texture="", x=10, y=10):
        index = len(nodes) + 1
        nodes.append(dict(id=index, kind=kind, a=a, b=b, color=rgb,
                          value=value, x=x, y=y, texture=texture))
        return index

    color_pin = node(0, rgb=color)
    if albedo:
        tex = node(2, texture=albedo, y=108)
        color_pin = node(3, a=color_pin, b=tex, x=210, y=10)
    normal_pin = node(2, texture=normal, x=210, y=107) if normal else 0
    return dict(format="jEngineMaterial", version=1, nodes=nodes, baseColor=color_pin,
                normal=normal_pin, metallic=node(1, value=metallic, x=210, y=222),
                roughness=node(1, value=roughness, x=210, y=310), surface=surface)


def convert_glb(source, target, pipeline):
    data = source.read_bytes()
    magic, version, length = struct.unpack_from("<4sII", data)
    json_length, chunk_type = struct.unpack_from("<I4s", data, 12)
    if magic != b"glTF" or version != 2 or length != len(data) or chunk_type != b"JSON":
        raise ValueError("Expected a glTF 2.0 binary")
    model = json.loads(data[20:20 + json_length])
    tail = data[20 + json_length:]

    def texture(info):
        if info is None:
            return ""
        index = model["textures"][info["index"]]["source"]
        uri = model["images"][index].get("uri", "")
        if not uri or uri.startswith("data:"):
            raise ValueError("This converter requires external GLB images")
        return copy_dependency(source.parent, target, uri)

    graphs = {}
    for index, material in enumerate(model.get("materials", [])):
        # The shipped Sponza GLB has no material names. Give every slot a stable
        # key in the staged GLB so both importers bind the same source material.
        name = f"slot_{index:03d}"
        material["name"] = name
        pbr = material.get("pbrMetallicRoughness", {})
        rgba = pbr.get("baseColorFactor", [1, 1, 1, 1])
        extensions = material.get("extensions", {})
        coat = extensions.get("KHR_materials_clearcoat", {})
        surface = {
            "opacity": rgba[3], "alphaMode": material.get("alphaMode", "OPAQUE").lower(),
            "alphaCutoff": material.get("alphaCutoff", .5), "doubleSided": material.get("doubleSided", False),
            "metallicRoughnessTexture": texture(pbr.get("metallicRoughnessTexture")),
            "emission": material.get("emissiveFactor", [0, 0, 0]),
            "emissionTexture": texture(material.get("emissiveTexture")),
            "transmission": extensions.get("KHR_materials_transmission", {}).get("transmissionFactor", 0),
            "ior": extensions.get("KHR_materials_ior", {}).get("ior", 1.5),
            "clearCoat": coat.get("clearcoatFactor", 0),
            "clearCoatGloss": 1 - coat.get("clearcoatRoughnessFactor", 1),
        }
        graphs[name] = graph(rgba[:3], pbr.get("metallicFactor", 1), pbr.get("roughnessFactor", 1),
                             texture(pbr.get("baseColorTexture")), texture(material.get("normalTexture")), surface)
    for buffer in model.get("buffers", []):
        if "uri" in buffer and not buffer["uri"].startswith("data:"):
            copy_dependency(source.parent, target, buffer["uri"])
    encoded = json.dumps(model, separators=(",", ":")).encode("utf-8")
    encoded += b" " * (-len(encoded) % 4)
    (target / source.name).write_bytes(
        struct.pack("<4sIII4s", b"glTF", 2, 20 + len(encoded) + len(tail), len(encoded), b"JSON")
        + encoded + tail)

    placement = source.with_name("objects.json")
    if placement.exists() or pipeline:
        settings = json.loads(placement.read_text(encoding="utf-8")) if placement.exists() else {}
        if pipeline:
            settings["renderer"] = pipeline
            settings["loader"] = "Model"
        write_json(target / "objects.json", settings)
    return graphs


def convert_legacy(source, target):
    scene = source.read_text(encoding="utf-8")
    blocks = re.finditer(r"(?m)^[ \t]*(material[ \t]+[^\r\n]+|mesh|renderer|camera|light)[ \t]*\r?\n[ \t]*\{([^}]*)\}", scene)
    graphs = {}
    scalar_fields = {
        "opacity": "opacity", "alphacutoff": "alphaCutoff", "spectrans": "transmission",
        "ior": "ior", "clearcoat": "clearCoat", "clearcoatgloss": "clearCoatGloss",
        "subsurface": "subsurface", "anisotropic": "anisotropy", "speculartint": "specularTint",
        "sheen": "sheen", "sheentint": "sheenTint",
    }
    for block in blocks:
        header = block[1].split()
        fields = dict(line.split(None, 1) for raw in block[2].splitlines()
                      if (line := raw.split("#", 1)[0].strip()) and len(line.split(None, 1)) == 2)
        if header[0] != "material":
            for key in ("file", "envmapfile"):
                if key in fields and fields[key] != "none":
                    copy_dependency(source.parent, target, fields[key])
            continue
        name = header[1]
        if name in graphs:
            continue
        surface = {output: float(fields[key]) for key, output in scalar_fields.items() if key in fields}
        if "alphamode" in fields:
            surface["alphaMode"] = fields["alphamode"].lower()
        for key, output in (("metallicroughnesstexture", "metallicRoughnessTexture"),
                            ("emissiontexture", "emissionTexture")):
            if key in fields and fields[key] != "none":
                surface[output] = copy_dependency(source.parent, target, fields[key])
        if "metallicRoughnessTexture" in surface:
            surface["legacyPackedMap"] = True
        surface["emission"] = [float(n) for n in fields.get("emission", "1 1 1" if "emissionTexture" in surface else "0 0 0").split()]
        albedo = fields.get("albedotexture", "none")
        normal = fields.get("normaltexture", "none")
        graphs[name] = graph([float(n) for n in fields.get("color", "1 1 1").split()],
                             float(fields.get("metallic", 0)), float(fields.get("roughness", .5)),
                             "" if albedo == "none" else copy_dependency(source.parent, target, albedo),
                             "" if normal == "none" else copy_dependency(source.parent, target, normal), surface)
    (target / source.name).write_text(scene, encoding="utf-8")
    return graphs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, help="new scene directory under the Scene Browser resource root")
    parser.add_argument("--pipeline", choices=("Deferred", "PathTracing"), help="override the staged GLB renderer")
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    if source.suffix.lower() not in (".scene", ".glb"):
        parser.error("source must be a .glb or .scene")
    if output.exists():
        parser.error("output directory must be new")
    output.parent.mkdir(parents=True, exist_ok=True)
    try:
        with tempfile.TemporaryDirectory(prefix=".material-stage-", dir=output.parent) as work:
            staged = Path(work) / "scene"
            staged.mkdir()
            graphs = convert_glb(source, staged, args.pipeline) if source.suffix.lower() == ".glb" else convert_legacy(source, staged)
            if not graphs:
                raise ValueError("The source scene has no materials")
            paths = {}
            for index, (name, material) in enumerate(graphs.items()):
                safe = re.sub(r"[^a-zA-Z0-9_-]", "_", name)
                path = f"Assets/Materials/{index:03d}_{safe}.jmaterial"
                write_json(staged / path, material)
                paths[name] = path
            write_json(staged / (source.name + ".materials.json"),
                       dict(format="jEngineSceneMaterials", version=1, materials=paths))
            write_json(staged / "project.jproject",
                       dict(schemaVersion=1, projectId=str(uuid.uuid4()), name=f"{source.stem} materials",
                            assetRoot="Assets", startupScene="", settings={}))
            staged.rename(output)
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.exit(1, f"Scene conversion failed: {error}\n")
    print(f"{source.name}: {len(graphs)} material graphs in {output}")


if __name__ == "__main__":
    main()
