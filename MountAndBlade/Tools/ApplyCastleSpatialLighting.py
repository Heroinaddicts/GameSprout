#!/usr/bin/env python3
"""Publish the castle's static capture flags and its baked lighting resources."""
import argparse
import json
from pathlib import Path
import shutil
import uuid


PROJECT = Path(__file__).resolve().parent.parent
LIGHTING = Path("MedievalCastle/Lighting")
CAPTURE_FIELDS = {"File", "CaptureEdge", "CaptureFar", "Grid", "MinimumCaptureY"}


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write(path, value):
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def mark_static_geometry(scene):
    changed = 0
    for node in scene["Objects"]:
        if node.get("Name") == "Moat water" or not node.get("StaticFlags", 0) & 128:
            continue
        if any(component["Type"] == "GameSproutRuntime.MeshRenderer" for component in node["Components"]):
            # Keep authored flags. Dynamic rider/horse and the mirror are excluded.
            node["StaticFlags"] |= 1 | 64
            changed += 1
    return changed


def attach(project, compiled, entries, ref_field, importer, type_id):
    result = []
    for entry in entries:
        relative = LIGHTING / entry["File"]
        source = compiled / entry["File"]
        destination = project / "Assets" / relative
        temporary = destination.with_name(destination.name + ".tmp")
        shutil.copyfile(source, temporary)
        temporary.replace(destination)
        guid = uuid.uuid5(uuid.NAMESPACE_URL, f"GameSprout:MedievalCastle:{relative.as_posix()}").hex
        write(Path(f"{destination}.meta"), {
            "Version": 1, "Guid": guid, "ImporterType": importer, "Path": relative.as_posix(),
            "ImportSettings": {}, "SubAssets": [
                {"Type": type_id, "SourceKey": "main", "LocalID": 1, "Name": destination.stem}
            ],
        })
        item = {key: val for key, val in entry.items() if key not in CAPTURE_FIELDS}
        item[ref_field] = {"Guid": guid, "LocalID": 1}
        result.append(item)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=PROJECT)
    parser.add_argument("--compiled", type=Path, help="publish already-baked environments and irradiance")
    args = parser.parse_args()
    args.project = args.project.resolve()
    scene_path = args.project / "Assets/Scenes/MedievalCastle.scene"
    scene = read(scene_path)
    manifest = read(args.project / "Tools/CastleSpatialLighting.json")
    marked = mark_static_geometry(scene)
    if not marked:
        raise SystemExit("No authored static mesh renderers found")
    print(f"Marked {marked} authored static renderers for reflection and irradiance capture.")
    if args.compiled:
        for entry in manifest["ReflectionProbes"] + manifest["IndirectLightVolumes"]:
            if not (args.compiled / entry["File"]).is_file():
                raise SystemExit(f"Baked asset missing: {entry['File']}")
        lighting = scene["EnvironmentLighting"]
        lighting["PlanarReflection"] = manifest["PlanarReflection"]
        lighting["ReflectionProbes"] = attach(args.project, args.compiled, manifest["ReflectionProbes"],
                                              "Environment", "Environment", 33)
        lighting["IndirectLightVolumes"] = attach(args.project, args.compiled, manifest["IndirectLightVolumes"],
                                                  "Asset", "Irradiance", 34)
        water_path = args.project / "Assets/MedievalCastle/Materials/Moat Water.mat"
        water = read(water_path)
        water.update(PlanarReflection=True, Roughness=0.13, Metallic=0.12)
        write(water_path, water)
    write(scene_path, scene)


if __name__ == "__main__":
    main()
