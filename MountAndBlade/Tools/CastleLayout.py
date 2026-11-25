#!/usr/bin/env python3
"""Resolve the castle package's authored Unity scene and prefab placements."""
import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from functools import lru_cache
import json
import math
from pathlib import Path
import re


_HEADER = re.compile(r"^--- !u!(\d+) &(-?\d+)( stripped)?[ \t]*$", re.M)
_PROPERTY = re.compile(r"^  ([\w]+(?:[ \t]+[\w]+)*):[ \t]*(.*)$", re.M)
_POINTER = re.compile(r"\{fileID:\s*(-?\d+)(?:,\s*guid:\s*([0-9a-f]{32}))?")
_OVERRIDE = re.compile(
    r"^\s*- target: (?P<target>\{[^}]+\})\n"
    r"\s*propertyPath: (?P<path>[^\n]+)\n"
    r"\s*value:[ \t]*(?P<value>[^\n]*)\n"
    r"\s*objectReference: (?P<reference>\{[^}]+\})",
    re.M,
)
_MATERIAL_INDEX = re.compile(r"m_Materials\.Array\.data\[(\d+)\]\Z")
_XYZ = ("x", "y", "z")
_XYZW = (*_XYZ, "w")
_IDENTITY = (0.0, 0.0, 0.0, 1.0)
_ORIGIN = (0.0, 0.0, 0.0)
_UNIT = (1.0, 1.0, 1.0)
_TRANSFORM_FIELDS = (
    ("Position", "m_LocalPosition", _XYZ, _ORIGIN),
    ("Rotation", "m_LocalRotation", _XYZW, _IDENTITY),
    ("Scale", "m_LocalScale", _XYZ, _UNIT),
)


def pointer(text):
    match = _POINTER.search(text or "")
    return (match[2] or "", int(match[1])) if match else ("", 0)


def ref_json(value):
    return {"Guid": value[0], "FileID": value[1]}


def local_id(text):
    return pointer(text)[1]


def scalar(text):
    value = str(text).strip()
    if value.startswith('"') and value.endswith('"'):
        try:
            return json.loads(value)
        except json.JSONDecodeError:
            return value[1:-1]
    if value.startswith("'") and value.endswith("'"):
        return value[1:-1].replace("''", "'")
    return value


def number(text):
    value = float(scalar(text))
    if not math.isfinite(value):
        raise ValueError(f"Non-finite Unity scalar: {text}")
    return value


def vector(text, keys, fallback):
    if not text:
        return list(fallback)
    fields = dict(re.findall(r"([xyzwrgba]):\s*([^,}]+)", text))
    return [number(fields[key]) if key in fields else fallback[index]
            for index, key in enumerate(keys)]


@dataclass(frozen=True)
class Record:
    class_id: int
    file_id: int
    stripped: bool
    body: str
    properties: dict

    def get(self, name, default=""):
        return self.properties.get(name, default)

    def list_section(self, prop):
        match = re.search(r"^  " + re.escape(prop) + r":[ \t]*\n((?:  - [^\n]*\n)*)", self.body, re.M)
        return match[1] if match else ""


def records(text):
    headers = list(_HEADER.finditer(text))
    for index, header in enumerate(headers):
        end = headers[index + 1].start() if index + 1 < len(headers) else len(text)
        body = text[header.end():end]
        yield Record(int(header[1]), int(header[2]), header[3] is not None, body, dict(_PROPERTY.findall(body)))


def normalized(quaternion):
    length = math.sqrt(sum(value * value for value in quaternion))
    if length < 1e-8:
        raise ValueError("Invalid zero Unity rotation quaternion")
    return [value / length for value in quaternion]


def multiply_rotation(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    ]


def local_matrix(transform):
    x, y, z, w = transform["Rotation"]
    tx, ty, tz = transform["Position"]
    sx, sy, sz = transform["Scale"]
    return [
        (1 - 2 * (y * y + z * z)) * sx, 2 * (x * y - z * w) * sy, 2 * (x * z + y * w) * sz, tx,
        2 * (x * y + z * w) * sx, (1 - 2 * (x * x + z * z)) * sy, 2 * (y * z - x * w) * sz, ty,
        2 * (x * z - y * w) * sx, 2 * (y * z + x * w) * sy, (1 - 2 * (x * x + y * y)) * sz, tz,
        0.0, 0.0, 0.0, 1.0,
    ]


def multiply_matrix(a, b):
    return [sum(a[row * 4 + offset] * b[offset * 4 + col] for offset in range(4))
            for row in range(4) for col in range(4)]


@dataclass(eq=False)
class Node:
    name: str
    source_name: str
    active: bool
    transform: dict
    prefab_guid: str
    parent: object = None

    @lru_cache(maxsize=None)
    def world(self):
        matrix = local_matrix(self.transform)
        if self.parent is None:
            return {**self.transform, "WorldMatrix": matrix}
        world = self.parent.world()
        world_matrix = multiply_matrix(world["WorldMatrix"], matrix)
        return {
            "Position": [world_matrix[3], world_matrix[7], world_matrix[11]],
            "Rotation": normalized(multiply_rotation(world["Rotation"], self.transform["Rotation"])),
            "Scale": [a * b for a, b in zip(world["Scale"], self.transform["Scale"])],
            "WorldMatrix": world_matrix,
        }

    @lru_cache(maxsize=None)
    def chain(self):
        local = {"Name": self.name, **self.transform}
        return [*self.parent.chain(), local] if self.parent else [local]

    @lru_cache(maxsize=None)
    def hierarchy_path(self):
        return "/".join(part["Name"] for part in self.chain())

    @lru_cache(maxsize=None)
    def enabled(self):
        return self.active and (self.parent is None or self.parent.enabled())

    def placement(self):
        return {
            "Name": self.name,
            "SourceName": self.source_name,
            "Path": self.hierarchy_path(),
            "PrefabGuid": self.prefab_guid,
            "TransformChain": self.chain(),
            **self.world(),
        }


@dataclass
class Scope:
    guid: str
    instance_path: str
    overrides: dict = field(default_factory=dict)
    nodes: dict = field(default_factory=dict)
    by_game_object: dict = field(default_factory=dict)
    lookup: dict = field(default_factory=dict)
    nested: dict = field(default_factory=dict)

    def value(self, record, name, default=""):
        value = self.overrides.get(record.file_id, {}).get(name)
        return value[0] if value is not None else record.get(name, default)

    def transform(self, record):
        result = {}
        for key, prop, axes, fallback in _TRANSFORM_FIELDS:
            values = vector(record.get(prop), axes, fallback)
            for index, axis in enumerate(axes):
                entry = self.overrides.get(record.file_id, {}).get(prop + "." + axis)
                if entry is not None:
                    values[index] = number(entry[0])
            result[key] = normalized(values) if key == "Rotation" else values
        return result


class Resolver:
    def __init__(self, manifest, metadata, groups):
        self.entries = {entry["guid"]: entry for entry in manifest}
        self.metadata = metadata
        self.groups = set(groups)
        self.visual_records = []
        self.collider_records = []
        self.camera_records = []
        self.light_records = []
        self.terrain_records = []
        self.messages = Counter()
        self.documents = {}

    def load(self, guid):
        if guid not in self.documents:
            path = self.metadata / (guid + ".yaml")
            if not path.is_file():
                raise FileNotFoundError(f"Missing package YAML {guid} for {self.entries.get(guid)}")
            self.documents[guid] = list(records(path.read_text(encoding="utf-8-sig")))
        return self.documents[guid]

    @staticmethod
    def overrides(instance):
        values = defaultdict(dict)
        for match in _OVERRIDE.finditer(instance.body):
            source, file_id = pointer(match["target"])
            values[source][file_id, scalar(match["path"])] = (match["value"], pointer(match["reference"]))
        return values

    def scope(self, guid, instance_path, inherited_edits=None, seen=()):
        if guid in seen:
            raise ValueError("Prefab dependency cycle: " + " -> ".join((*seen, guid)))
        scope = Scope(guid, instance_path)
        for (file_id, prop), edit in (inherited_edits or {}).get(guid, {}).items():
            scope.overrides.setdefault(file_id, {})[prop] = edit
        docs = self.load(guid)
        objects = {record.file_id: record for record in docs if record.class_id == 1 and not record.stripped}
        transforms = [record for record in docs if record.class_id == 4]
        for record in transforms:
            if record.stripped:
                continue
            game_object_id = local_id(record.get("m_GameObject"))
            source = objects[game_object_id]
            node = Node(
                scalar(scope.value(source, "m_Name", "Unnamed")),
                scalar(source.get("m_Name", "Unnamed")),
                scope.value(source, "m_IsActive", "1").strip() != "0",
                scope.transform(record),
                guid if seen else "",
            )
            scope.nodes[record.file_id] = node
            scope.by_game_object[game_object_id] = node
            scope.lookup[record.file_id] = node
        for instance in docs:
            if instance.class_id != 1001:
                continue
            nested_guid, _ = pointer(instance.get("m_SourcePrefab"))
            if self.entries.get(nested_guid, {}).get("ext") != ".prefab":
                self.messages["Unsupported nested source dependency"] += 1
                continue
            edits = self.overrides(instance)
            # Outer override of a nested prefab supersedes its direct instance values.
            for target, properties in (inherited_edits or {}).items():
                if target in edits or target == nested_guid:
                    edits[target].update(properties)
            child = self.scope(nested_guid, f"{instance_path}/{instance.file_id}", edits, (*seen, guid))
            scope.nested[instance.file_id] = (instance, child)
        for record in transforms:
            if not record.stripped:
                continue
            child = scope.nested.get(local_id(record.get("m_PrefabInstance")))
            source_id = local_id(record.get("m_CorrespondingSourceObject"))
            node = child[1].lookup.get(source_id) if child else None
            if node is not None:
                scope.lookup[record.file_id] = node
        for record in transforms:
            if record.stripped:
                continue
            parent_id = local_id(record.get("m_Father"))
            scope.nodes[record.file_id].parent = scope.lookup.get(parent_id)
            if parent_id and parent_id not in scope.lookup:
                self.messages["Unresolved local transform parent"] += 1
        for instance, child in scope.nested.values():
            parent_match = re.search(r"^    m_TransformParent: (\{[^}]+\})", instance.body, re.M)
            parent_id = local_id(parent_match[1]) if parent_match else 0
            parent = scope.lookup.get(parent_id)
            if parent_id and parent is None:
                self.messages["Unresolved prefab attachment"] += 1
            if parent:
                for node in child.nodes.values():
                    if node.parent is None:
                        node.parent = parent
        return scope

    @staticmethod
    def lod_levels(docs):
        by_renderer = {}
        for record in docs:
            if record.class_id != 205 or record.get("m_Enabled", "1") == "0":
                continue
            lod = -1
            for line in record.body.splitlines():
                content = line.strip()
                if content.startswith("- screenRelativeHeight:"):
                    lod += 1
                if content.startswith("- renderer:") and lod >= 0:
                    renderer = local_id(content)
                    if renderer:
                        by_renderer.setdefault(renderer, (record.file_id, []))[1].append(lod)
        return by_renderer

    def mesh_reference(self, value, node):
        guid, file_id = value
        if not file_id or self.entries.get(guid, {}).get("ext") != ".fbx":
            if file_id:
                self.messages["Unsupported non-FBX mesh reference"] += 1
            return None
        return {"Guid": guid, "FileID": file_id, "NameHint": node.source_name}

    def capture(self, scope):
        docs = [record for record in self.load(scope.guid) if not record.stripped]
        by_filter = {local_id(record.get("m_GameObject")): record for record in docs if record.class_id == 33}
        lod_levels = self.lod_levels(docs)
        for record in docs:
            game_object_id = local_id(record.get("m_GameObject"))
            node = scope.by_game_object.get(game_object_id)
            if node is None or not node.enabled() or scope.value(record, "m_Enabled", "1") == "0":
                continue
            if record.class_id in (23, 64, 65, 135, 136, 137):
                if node.chain()[0]["Name"] not in self.groups:
                    continue
            if record.class_id in (23, 137):
                mesh_filter = by_filter.get(game_object_id) if record.class_id == 23 else record
                if not mesh_filter:
                    continue
                mesh = self.mesh_reference(pointer(mesh_filter.get("m_Mesh")), node)
                if not mesh:
                    continue
                level = lod_levels.get(record.file_id)
                materials = [pointer(line) for line in record.list_section("m_Materials").splitlines()]
                for prop, value in scope.overrides.get(record.file_id, {}).items():
                    match = _MATERIAL_INDEX.fullmatch(prop)
                    if match:
                        slot = int(match[1])
                        if slot >= len(materials):
                            materials.extend([("", 0)] * (slot + 1 - len(materials)))
                        materials[slot] = value[1]
                self.visual_records.append({
                    **node.placement(), "Mesh": mesh,
                    "Materials": [ref_json(material) for material in materials],
                    "Lod": min(level[1]) if level else None,
                    "LodLevels": level[1] if level else [],
                    "LodGroup": f"{scope.instance_path}/{level[0]}" if level else None,
                    "ShadowCasting": ("Off", "On", "TwoSided", "ShadowsOnly")[int(scope.value(record, "m_CastShadows", "1"))],
                    "ReceiveShadows": scope.value(record, "m_ReceiveShadows", "1") == "1",
                })
                if record.class_id == 137:
                    self.messages["Static display of authored skinned props"] += 1
            elif record.class_id in (64, 65, 135, 136):
                collider = self.collider(scope, record, node, by_filter.get(game_object_id))
                if collider:
                    self.collider_records.append(collider)
            elif record.class_id == 20:
                self.camera_records.append({
                    **node.placement(), "Fov": number(record.get("field of view", "60")),
                    "Near": number(record.get("near clip plane", "0.3")),
                    "Far": number(record.get("far clip plane", "1000")),
                })
            elif record.class_id == 108:
                self.light_records.append({
                    **node.placement(), "LightType": int(record.get("m_Type", "1")),
                    "Color": vector(record.get("m_Color"), ("r", "g", "b", "a"), (1, 1, 1, 1)),
                    "Intensity": number(record.get("m_Intensity", "1")),
                })
            elif record.class_id == 218:
                self.terrain_records.append({**node.placement(), "Data": ref_json(pointer(record.get("m_TerrainData")))})
        for _, child in scope.nested.values():
            self.capture(child)

    def collider(self, scope, record, node, mesh_filter):
        data = {
            **node.placement(),
            "Type": {64: "Mesh", 65: "Box", 135: "Sphere", 136: "Capsule"}[record.class_id],
            "Trigger": scope.value(record, "m_IsTrigger", "0") == "1",
        }
        if record.class_id == 64:
            value = pointer(record.get("m_Mesh"))
            mesh = self.mesh_reference(value, node)
            if not mesh:
                return None
            return {**data, "Mesh": mesh, "Convex": scope.value(record, "m_Convex", "0") == "1",
                    "SharingRenderMesh": bool(mesh_filter and value == pointer(mesh_filter.get("m_Mesh")))}
        data["Center"] = vector(record.get("m_Center"), _XYZ, _ORIGIN)
        if record.class_id == 65:
            data["Size"] = vector(record.get("m_Size"), _XYZ, _UNIT)
        if record.class_id in (135, 136):
            data["Radius"] = number(record.get("m_Radius", "0.5"))
        if record.class_id == 136:
            data["Height"] = number(record.get("m_Height", "2"))
            data["Axis"] = "XYZ"[int(record.get("m_Direction", "1"))]
        return data

    def layout(self, scene_guid):
        root = self.scope(scene_guid, self.entries[scene_guid]["path"])
        self.capture(root)
        return {
            "Version": 1,
            "CoordinateSystem": "LeftHandedYUp",
            "Scene": self.entries[scene_guid]["path"],
            "Groups": sorted(self.groups),
            "Visuals": self.visual_records,
            "Colliders": self.collider_records,
            "Cameras": self.camera_records,
            "Lights": self.light_records,
            "Terrains": self.terrain_records,
            "Diagnostics": dict(self.messages),
        }


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("manifest", type=Path)
    cli.add_argument("metadata", type=Path)
    cli.add_argument("output", type=Path)
    cli.add_argument("--scene", default="MC_Demo_Day.unity")
    cli.add_argument("--groups", nargs="+", default=["Castle"], metavar="GROUP",
                     help="Top-level authored groups whose meshes and collision are included")
    args = cli.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    matches = [entry for entry in manifest if entry["path"].rsplit("/", 1)[-1] == args.scene]
    if len(matches) != 1:
        cli.error(f"Expected exactly one packaged scene named {args.scene}")
    result = Resolver(manifest, args.metadata, args.groups).layout(matches[0]["guid"])
    if not result["Visuals"]:
        cli.error("Selected groups contain no visible FBX meshes")
    args.output.write_text(json.dumps(result, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
    print(f"{len(result['Visuals'])} visual poses, {len(result['Colliders'])} collision shapes, "
          f"{len(result['Cameras'])} cameras, {len(result['Terrains'])} terrains")


if __name__ == "__main__":
    main()
