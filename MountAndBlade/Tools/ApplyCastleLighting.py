#!/usr/bin/env python3
"""Install the baked Day sky and author the main castle's local lighting."""
import argparse
import json
from pathlib import Path
import shutil
import uuid

from BuildTour import rotation, save


def _author_lights(scene, lights):
    objects = scene["Objects"]
    by_name = {node["Name"]: node for node in objects}
    for name, (position, orientation, settings) in lights.items():
        node = by_name.get(name)
        if node is None:
            next_id = scene["NextObjectID"]
            node = {"ID": next_id, "ParentID": 0, "Name": name, "Active": True, "Components": [
                {"ID": next_id + 1, "Type": "GameSproutRuntime.Transform", "Scale": [1, 1, 1]},
                {"ID": next_id + 2, "Type": "GameSproutRuntime.Light"},
            ]}
            scene["NextObjectID"] += 3
            objects.append(node)
        node["Components"][0].update(Position=position, Rotation=orientation)
        node["Components"][1]["Settings"] = settings


def apply_mounted_lighting(scene):
    objects = scene["Objects"]
    player = next(node for node in objects if node["Name"] == "Mount")
    mounted_ids = {player["ID"]}
    pending = list(objects)
    while pending:
        next_level = []
        advanced = False
        for node in pending:
            if node["ParentID"] not in mounted_ids:
                next_level.append(node)
                continue
            mounted_ids.add(node["ID"])
            advanced = True
            if any(component["Type"] == "GameSproutRuntime.MeshRenderer" for component in node["Components"]):
                node["Layer"] = 9
        if not advanced:
            break
        pending = next_level
    planar = scene["EnvironmentLighting"].get("PlanarReflection")
    if planar is not None:
        planar["LayerMask"] = planar.get("LayerMask", 0xffffffff) & ~(1 << 8)
    sun = next(node for node in objects if node["Name"] == "Castle afternoon sun")
    shadow = sun["Components"][1]["Settings"]["Shadows"]
    shadow.update(Distance=95, Resolution=4096, CascadeSplits=[.105, .235, .50],
                  Strength=.83, DepthBias=2.6, NormalBias=2.2, FilterQuality="High")
    mounted_layers = (1 << 8) | (1 << 9)
    _author_lights(scene, {
        "Cavalry daylight fill": ([0, 0, 0], [.232361, .474809, -.131561, .838604],
            {"Type": "Directional", "Color": [.88, .94, 1, 1], "Intensity": .85,
             "CullingMask": mounted_layers}),
        "Cavalry sky return": ([0, 0, 0], [.0178464, .95254, -.298506, .0569481],
            {"Type": "Directional", "Color": [.80, .90, 1, 1], "Intensity": .62,
             "CullingMask": mounted_layers}),
    })


def apply(project, environment_asset):
    relative = "MedievalCastle/Lighting/Medieval Day.environment"
    path = project / "Assets" / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    if environment_asset.resolve() != path.resolve():
        shutil.copyfile(environment_asset, path)
    guid = uuid.uuid5(uuid.NAMESPACE_URL, "gamesprout:mount-and-blade:" + relative).hex
    save(path.with_suffix(".environment.meta"), {
        "Version": 1, "Guid": guid, "Path": relative, "ImporterType": "Environment",
        "ImportSettings": {}, "SubAssets": [{"Type": 33, "SourceKey": "main", "LocalID": 1, "Name": path.stem}],
    })

    scene_path = project / "Assets/Scenes/MedievalCastle.scene"
    scene = json.loads(scene_path.read_text(encoding="utf-8"))
    objects = scene["Objects"]
    environment = scene["EnvironmentLighting"]
    # The EXR panorama has a different longitude origin from the Unity cube.
    # Set yaw in GameSprout space so the visible sun is over the caster direction.
    environment.update(Mode="SkyEnvironment", Intensity=.72, SkyRotationDegrees=24.0,
                       SkyEnvironment={"Guid":guid, "LocalID":1}, SkyIntensity=1.05,
                       ReflectionIntensity=.64, HDREnabled=True, ExposureEV=-.25, Tonemap="Neutral",
                       SpotShadows={"MaxLights": 2, "MaxResolution": 1024})
    environment["Atmosphere"].update(FogStart=105, FogEnd=390, FogMaxOpacity=.58,
                                      FogColor=[.36, .45, .53, 1])
    sun = objects[0]
    assert sun["Name"] == "Castle afternoon sun"
    sun["Components"][0]["Rotation"] = rotation(45, -30)
    sun["Components"][1]["Settings"].update(Color=[1.0, .92, .79, 1.0], Intensity=2.10)

    lights = {
        "Keep lamp west": ([-31.288, 5.305, 30.529], (0, 0),
            {"Type": "Point", "Color": [.70, .31, .065, 1], "Intensity": 2.25, "Range": 1.8777046}),
        "Keep lamp east": ([-29.788, 5.305, 30.529], (0, 0),
            {"Type": "Point", "Color": [.70, .31, .065, 1], "Intensity": 2.25, "Range": 1.8777046}),
        "Gate lantern spill": ([4.6, 2.9, 1.44], (32, -112),
            {"Type": "Spot", "Color": [1, .64, .31, 1], "Intensity": 7.0, "Range": 8.5,
             "InnerSpotAngleDegrees": 45, "OuterSpotAngleDegrees": 82,
             "Shadows": {"Enabled": True, "Strength": .8, "Resolution": 512, "NearPlane": .13,
                         "DepthBias": .5, "NormalBias": .85, "Soft": True, "Priority": 80}}),
        "Courtyard lantern spill": ([-11.75, 4.2, 4.1], (37, 165),
            {"Type": "Spot", "Color": [1, .63, .32, 1], "Intensity": 6.5, "Range": 9,
             "InnerSpotAngleDegrees": 38, "OuterSpotAngleDegrees": 78,
             "Shadows": {"Enabled": True, "Strength": .8, "Resolution": 512, "NearPlane": .15,
                         "DepthBias": .5, "NormalBias": .9, "Soft": True, "Priority": 30}}),
    }
    objects[:] = [node for node in objects if node["Name"] != "Castle sky"]
    _author_lights(scene, {
        name: (position, rotation(*angles), settings)
        for name, (position, angles, settings) in lights.items()
    })
    apply_mounted_lighting(scene)

    textures = project / "Assets/MedievalCastle/Textures"
    for meta_path in textures.glob("**/*.png.meta"):
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
        settings = meta["ImportSettings"]
        if settings.get("FilterMode") == "Trilinear" and meta_path.name.startswith(
                ("MC_Castle_Wall", "MC_Castle_Stone_Floor")):
            settings["Anisotropy"] = 8
            save(meta_path, meta)
    save(scene_path, scene)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("environment", type=Path)
    arguments = parser.parse_args()
    apply(arguments.project, arguments.environment)
