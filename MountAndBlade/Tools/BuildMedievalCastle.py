"""Assemble the licensed day castle into a separate native GameSprout scene."""

from ApplyCastleLighting import apply_mounted_lighting

import argparse
import json
import math
from pathlib import Path
import shutil
import subprocess

from BuildLandscape import _Mesh, _reference, _render, _smooth
from BuildTour import Scene, add_gameplay, rotation, save, write_asset


SCENE_PATH = "Scenes/MedievalCastle.scene"
MESH_FOLDER = "MedievalCastle/Ground"
SPAWN_X, SPAWN_Z = 38.0, 0.0
HEADING = -90.0
CAVALRY_SCRIPT_GUID = "6890c5dd5bb74f45b3374b48d0062a93"


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def reference(guid, local_id=1):
    return {"Guid": guid, "LocalID": local_id}


def copy_source(root, source, relative, guid, kind, settings, subassets):
    target = root / "Assets" / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    save(Path(str(target) + ".meta"), {
        "Version": 1, "Guid": guid, "Path": relative, "ImporterType": kind,
        "ImportSettings": settings, "SubAssets": subassets,
    })
    return target


def import_resources(root, source, models):
    textures = {}
    for guid, entry in read(source / "Textures.json").items():
        relative = "MedievalCastle/Textures/" + Path(entry["Path"]).parent.name + "/" + Path(entry["Path"]).name
        normal = entry["NormalMap"]
        stone_tile = Path(relative).stem.startswith(("MC_Castle_Wall", "MC_Castle_Stone_Floor"))
        # Current native texture sampling reads the first mip. Prefilter the
        # repeating masonry sheets so they stay less noisy on distant walls.
        limit = min(entry["MaxSize"], 512 if normal else 1024) if stone_tile else entry["MaxSize"]
        target = copy_source(root, source / entry["Path"], relative, guid, "Texture",
                    {"ColorSpace": entry["ColorSpace"], "AlphaSource": "None" if normal else "FromSource",
                     "GenerateMipmaps": True, "FilterMode": "Trilinear", "WrapU": "Repeat", "WrapV": "Repeat",
                     "MaxSize": limit},
                    [{"Type": 1, "SourceKey": "main", "LocalID": 1, "Name": Path(relative).stem}])
        if limit < entry["MaxSize"]:
            subprocess.run(["/usr/bin/sips", "-Z", str(limit), str(target)], check=True, capture_output=True)
        textures[entry["Path"]] = reference(guid)

    materials = read(source / "Materials.json")
    for guid, entry in materials.items():
        name = entry["Name"]
        relative = "MedievalCastle/Materials/" + name + ".mat"
        path = root / "Assets" / relative
        body = {key: entry[key] for key in ("BaseColor", "BaseColorTextureST", "NormalTextureST",
                "Roughness", "Metallic", "NormalScale", "DoubleSided", "Emission")}
        body.update({"Version": 1, "Name": name, "Shader": "GameSprout/Lit",
                     "EnableGPUInstancing": True})
        if name.startswith(("MC_Castle_Wall", "MC_Castle_Stone_Floor")):
            body["NormalScale"] *= .55
        for slot in ("BaseColorTexture", "NormalTexture"):
            if entry[slot]:
                body[slot] = textures[entry[slot]]
        # Native Lit has opaque rendering. Keep stained glass readable under
        # the sun with a subtle emissive tint until a transparent pass is used.
        if entry["AlphaMode"] == "Blend":
            body["Emission"] = [.023, .020, .013, 1.0]
        save(path, body)
        save(Path(str(path) + ".meta"), {
            "Version": 1, "Guid": guid, "Path": relative, "ImporterType": "Material", "ImportSettings": {},
            "SubAssets": [{"Type": 5, "SourceKey": "main", "LocalID": 1, "Name": name}],
        })

    for guid, model in models.items():
        path = Path(model["UnityPath"])
        relative = "MedievalCastle/Models/" + path.name
        identities = [{"Type": asset["Type"], "SourceKey": asset["SourceKey"], "Name": asset["Name"],
                       "LocalID": asset["Ref"]["LocalID"]}
                      for asset in model["SubAssets"] if asset["Type"] in (3, 4)]
        copy_source(root, source / path, relative, guid, "Model",
                    {"ImportMaterials": False, "ImportAnimation": False}, identities)
    return materials


def resolve_mesh(source_mesh, models):
    guid = source_mesh["Guid"]
    model = models[guid]
    name = model["ResolvedMeshNames"].get(str(source_mesh["FileID"]))
    if name is None:
        return None
    for node in model["Model"]["Nodes"]:
        if node["Name"] == name and "Mesh" in node:
            return reference(guid, node["Mesh"]["LocalID"])
    raise ValueError(f"Missing model mesh {name} in {model['UnityPath']}")


class CastleNodes:
    """Share prefab ancestors while retaining each source local transform."""

    def __init__(self, scene):
        self.scene = scene
        self.root = scene.node("Medieval Castle", static=True)["ID"]
        self.groups = {}

    def add(self, record, components, collision=False):
        parent = self.root
        for transform in record["TransformChain"][:-1]:
            key = (parent, transform["Name"], tuple(transform["Position"]),
                   tuple(transform["Rotation"]), tuple(transform["Scale"]))
            if key not in self.groups:
                self.groups[key] = self.scene.node(transform["Name"], parent,
                    position=transform["Position"], rotation=transform["Rotation"],
                    scale=transform["Scale"], static=True)["ID"]
            parent = self.groups[key]
        last = record["TransformChain"][-1]
        suffix = " [Collision]" if collision else ""
        self.scene.node(last["Name"] + suffix, parent, position=last["Position"],
            rotation=last["Rotation"], scale=last["Scale"], components=components, static=True)


def add_castle(scene, layout, models, materials, repairs):
    nodes = CastleNodes(scene)
    visuals = 0
    for item in layout["Visuals"]:
        if item["Lod"] not in (None, 0):
            continue
        mesh = resolve_mesh(item["Mesh"], models)
        if mesh is None:
            raise ValueError(f"Unresolved visual mesh at {item['Path']}")
        slots = [reference(slot["Guid"]) for slot in item["Materials"]]
        if any(slot["Guid"] not in materials for slot in slots):
            raise ValueError(f"Unresolved material slot at {item['Path']}")
        components = _render(mesh, slots)
        components[1].update({"ShadowCastingMode": item["ShadowCasting"],
                              "ReceiveShadows": item["ReceiveShadows"]})
        nodes.add(item, components)
        visuals += 1

    colliders = 0
    for item in layout["Colliders"]:
        kind = item["Type"]
        if kind == "Mesh":
            mesh = resolve_mesh(item["Mesh"], models)
            if mesh is None and item["Mesh"]["FileID"] == 5035141505552214146:
                # The source gate has a stale root mesh reference. Its authored
                # collider child is resolved and preserves the open passage.
                continue
            if mesh is None:
                raise ValueError(f"Unresolved collision mesh at {item['Path']}")
            key = f"{mesh['Guid']}_{mesh['LocalID']}"
            shape = {"Mesh": repairs.get(key, mesh), "Convex": item["Convex"]}
        elif kind == "Box":
            shape = {"Size": item["Size"]}
        elif kind == "Sphere":
            shape = {"Radius": item["Radius"]}
        elif kind == "Capsule":
            shape = {"Radius": item["Radius"], "Height": item["Height"],
                     "Axis": item["Axis"]}
        else:
            raise ValueError(f"Unsupported source collider {kind}")
        settings = {"IsTrigger": item["Trigger"]}
        if "Center" in item:
            settings["Center"] = item["Center"]
        nodes.add(item, [{"Type": "GameSproutRuntime." + kind + "Collider",
                         "Settings": settings, "Shape": shape}], collision=True)
        colliders += 1
    return visuals, colliders


class Ground:
    def __init__(self, data):
        self.origin = data["Origin"]
        self.step_x, self.step_z = data["Step"]
        self.resolution = data["Resolution"]
        self.samples = data["Samples"]
        self.scale = data["ScaleY"]
        if self.resolution != 257 or len(self.samples) != self.resolution ** 2:
            raise ValueError("Unexpected castle terrain dimensions")

    def height(self, ix, iz):
        return self.origin[1] + self.samples[iz * self.resolution + ix] * self.scale

    def nearest_height(self, x, z):
        ix = round((x - self.origin[0]) / self.step_x)
        iz = round((z - self.origin[2]) / self.step_z)
        return self.height(ix, iz)

    def add(self, root, scene):
        parent = scene.node("Castle terrain and approach", static=True)["ID"]
        ground = _reference("Environment/Materials/PackedEarth.mat")
        for tz in range(4):
            for tx in range(4):
                mesh = _Mesh()
                for lz in range(65):
                    iz = tz * 64 + lz
                    z = self.origin[2] + iz * self.step_z
                    for lx in range(65):
                        ix = tx * 64 + lx
                        x = self.origin[0] + ix * self.step_x
                        y = self.height(ix, iz)
                        dx = self.height(min(256, ix + 1), iz) - self.height(max(0, ix - 1), iz)
                        dz = self.height(ix, min(256, iz + 1)) - self.height(ix, max(0, iz - 1))
                        normal = (-dx / self.step_x, 2, -dz / self.step_z)
                        slope = math.hypot(normal[0], normal[2]) / 2
                        tone = .91 + .09 * math.sin(.17 * x + .05 * z) * math.sin(.11 * z)
                        lane = _smooth(-20, -15, x) * (1 - _smooth(76, 86, x))
                        lane *= 1 - _smooth(2.15, 3.8, abs(z + .9 * math.sin(x * .16)))
                        yard = _smooth(-43, -38, x) * (1 - _smooth(-8, -3, x))
                        yard *= _smooth(-18, -15, z) * (1 - _smooth(18, 22, z))
                        bank = 1 - _smooth(-2.6, .05, y)
                        soil = max(lane, yard, bank, _smooth(.38, 1.06, slope))
                        field = (.57, 1.0, .65)
                        earth = (.93, .81, .68)
                        tint = [tone * (a + (b - a) * soil) for a, b in zip(field, earth)]
                        mesh.vertex((x, y, z), normal, (x / 5.6, z / 5.6), (*tint, 1))

                for lz in range(64):
                    for lx in range(64):
                        a = lz * 65 + lx
                        b, c, d = a + 1, a + 65, a + 66
                        mesh.triangle(a, c, d)
                        mesh.triangle(a, d, b)

                asset = mesh.write(root, f"CastleTerrain_{tx}_{tz}", MESH_FOLDER)
                components = _render(asset, [ground])
                components.append({"Type": "GameSproutRuntime.MeshCollider",
                                   "Shape": {"Mesh": asset, "Convex": False}})
                scene.node(f"Castle terrain {tx + 1}/{tz + 1}", parent, components=components, static=True)

        self.add_moat(root, scene, parent)

    def add_moat(self, root, scene, parent):
        mesh = _Mesh()
        waterline = -2.75
        vertices = {}
        for iz in range(256):
            z = self.origin[2] + iz * self.step_z
            if not -61 <= z <= 62:
                continue
            for ix in range(256):
                x = self.origin[0] + ix * self.step_x
                if not -98 <= x <= 37:
                    continue
                corners = ((ix, iz), (ix, iz + 1), (ix + 1, iz + 1), (ix + 1, iz))
                if min(self.height(a, b) for a, b in corners) >= waterline:
                    continue
                for cell in corners:
                    if cell not in vertices:
                        wx = self.origin[0] + cell[0] * self.step_x
                        wz = self.origin[2] + cell[1] * self.step_z
                        dx = .042 * math.sin(wx * 1.66 + wz * .57)
                        dz = .038 * math.cos(wx * .41 - wz * 1.41)
                        vertices[cell] = mesh.vertex((wx, waterline, wz), (dx, 1, dz))
                a, b, c, d = (vertices[cell] for cell in corners)
                mesh.triangle(a, b, c)
                mesh.triangle(a, c, d)
        if not mesh.positions:
            return
        material = write_asset(root, "MedievalCastle/Materials/Moat Water.mat", "Material", 5, {
            "Version": 1, "Name": "Moat Water", "Shader": "GameSprout/Lit", "BaseColor": [.067, .126, .114, 1],
            "Metallic": 0, "Roughness": .28, "EnableGPUInstancing": True,
        })
        components = _render(mesh.write(root, "CastleMoat", MESH_FOLDER), [material])
        components[1]["ShadowCastingMode"] = "Off"
        scene.node("Moat water", parent, components=components, static=True)

def add_player(root, scene, tour_assets, ground):
    camera = add_gameplay(root, scene, tour_assets)
    mount = next(node for node in scene.objects if node["Name"] == "Mount")
    mount["Components"][0]["Position"] = [SPAWN_X, ground.nearest_height(SPAWN_X, SPAWN_Z) + .11, SPAWN_Z]
    script = mount["Components"][-1]["Fields"]
    script["HorseRider::Heading"] = {"Kind": "Number", "Value": HEADING}
    script["HorseRider::CameraDistance"] = {"Kind": "Number", "Value": 5.6}
    script["HorseRider::CameraPitch"] = {"Kind": "Number", "Value": 5.5}
    script["HorseRider::CameraHeight"] = {"Kind": "Number", "Value": 1.95}
    camera["Components"][0].update({"Position": [49.0, 8.3, -7.0], "Rotation": rotation(2.8, -82)})
    projection = camera["Components"][1]["Settings"]
    projection.update({"VerticalFovRadians": math.radians(58), "FarPlane": 620.0})
    sun = scene.objects[0]
    sun["Name"] = "Castle afternoon sun"
    sun["Components"][0]["Rotation"] = rotation(40, -115)
    sun["Components"][1]["Settings"].update({"Color": [1.0, .92, .80, 1.0], "Intensity": 1.48})
    sun["Components"][1]["Settings"]["Shadows"].update({"Distance": 110, "Strength": .94})
    return camera


def cavalry_asset_fields(root):
    def reference_for(path, source_key):
        meta = read(root / "Assets" / (path + ".meta"))
        subasset = next(asset for asset in meta["SubAssets"] if asset["SourceKey"] == source_key)
        return {"Kind": "Resource", "Value": reference(meta["Guid"], subasset["LocalID"])}

    scout = write_asset(root, "Characters/Knight/Materials/Scout Surcoat.mat", "Material", 5, {
        "Version": 1, "Name": "Scout Surcoat", "Shader": "GameSprout/Lit",
        "BaseColor": [.34, .055, .042, 1], "Roughness": .88, "Metallic": 0,
    })
    return {
        "HorseModel": reference_for("Characters/Horse/Horse.fbx", "main"),
        "KnightModel": reference_for("Characters/Knight/KnightBody.fbx", "main"),
        "HelmetModel": reference_for("Characters/Knight/Helmet.fbx", "main"),
        "ScoutMaterial": {"Kind": "Resource", "Value": scout},
        "RiderCount": {"Kind": "Unsigned", "Value": 1000},
        "SwapEquipment": {"Kind": "Boolean", "Value": True},
        "DetailedShadows": {"Kind": "Boolean", "Value": False},
    }


def add_cavalry(root, scene):
    mount = next(node for node in scene.objects if node["Name"] == "Mount")
    fields = mount["Components"][-1]["Fields"]
    authoring = {
        "VisualTemplate": fields["HorseRider::Visual"],
        "CanterClip": fields["HorseRider::CanterClip"],
        "WalkClip": fields["HorseRider::WalkClip"],
        **cavalry_asset_fields(root),
        "CenterX": {"Kind": "Number", "Value": 101},
        "CenterZ": {"Kind": "Number", "Value": 0},
        "RadiusX": {"Kind": "Number", "Value": 62},
        "RadiusZ": {"Kind": "Number", "Value": 112},
        "ProfileMovement": {"Kind": "Boolean", "Value": False},
    }
    # Keep this root at the scene origin so the authored terrain zone is in world space.
    scene.node("Cavalry Stress", components=({
        "Type": "GameSproutRuntime.ScriptComponent",
        "InstanceID": 1,
        "ScriptTypeID": CAVALRY_SCRIPT_GUID,
        "Source": reference(CAVALRY_SCRIPT_GUID),
        "Enabled": True,
        "Fields": {f"CavalryStress::{key}": value for key, value in authoring.items()},
    },))


def build(root, source, layout, models, tour_assets, repair_root, replace=False):
    if (root / "Assets" / SCENE_PATH).exists() and not replace:
        raise ValueError(f"{SCENE_PATH} already exists. Use --replace to rebuild generated assets.")
    materials = import_resources(root, source, models)
    repairs = {key: reference(value["Guid"], value["LocalID"])
               for key, value in read(repair_root / "CollisionRepairs.json").items()}
    repair_folder = Path("Assets/MedievalCastle/Colliders")
    shutil.copytree(repair_root / repair_folder, root / repair_folder, dirs_exist_ok=True)
    ground = Ground(read(source / "TerrainHeight.json"))
    scene = Scene()
    camera = add_player(root, scene, tour_assets, ground)
    visuals, colliders = add_castle(scene, layout, models, materials, repairs)
    ground.add(root, scene)
    environment = {"Mode": "Gradient", "Intensity": 1, "SkyColor": [.27, .32, .39, 1],
                   "EquatorColor": [.18, .20, .24, 1], "GroundColor": [.115, .105, .092, 1],
                   "Atmosphere": {"Enabled": True, "DrawSky": True, "ZenithColor": [.070, .22, .42, 1],
                                  "HorizonColor": [.41, .54, .65, 1], "FogColor": [.39, .50, .59, 1],
                                  "FogStart": 120, "FogEnd": 350, "FogMaxOpacity": .76}}
    sky_components = _render(_reference("Environment/Meshes/ValleyCloudDome.mesh"),
                             [_reference("Environment/Materials/ValleyClouds.mat")])
    sky_components[1].update({"ShadowCastingMode": "Off", "ReceiveShadows": False})
    scene.node("Castle sky", position=(0, 700, 0), rotation=rotation(0, 29), components=sky_components, static=True)
    add_cavalry(root, scene)
    document = {"Version": 2, "Name": "Medieval Castle", "MainCameraID": camera["ID"],
                "NextObjectID": scene.next_id, "EnvironmentLighting": environment, "Objects": scene.objects}
    apply_mounted_lighting(document)
    write_asset(root, SCENE_PATH, "Scene", 13, document)
    return {"Objects": len(scene.objects), "CastleRenderers": visuals, "CastleColliders": colliders}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("source", type=Path)
    parser.add_argument("layout", type=Path)
    parser.add_argument("models", type=Path)
    parser.add_argument("repairs", type=Path, help="Validated repair assets and collision reference mapping")
    parser.add_argument("--replace", action="store_true")
    arguments = parser.parse_args()
    result = build(arguments.root, arguments.source, read(arguments.layout), read(arguments.models),
                   read(Path(__file__).with_name("TourAssets.json")), arguments.repairs, arguments.replace)
    print(json.dumps(result))
