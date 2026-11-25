#!/usr/bin/env python3
"""Original, deterministic countryside for the first horseback exploration scene.

Only the Python standard library is required. Geometry and textures are authored
here, rather than copied from a commercial pack. ``build_landscape(root, node)``
writes current GameSprout source assets and creates scene objects through the
caller's node factory; it does not own the scene, camera, rider or lighting.
World units are metres, Y is up and the road initially travels towards +Z.
"""

import hashlib
import json
import math
from pathlib import Path
import random
import struct
import uuid
import zlib


_NAMESPACE = uuid.UUID("9f934d62-909f-4f82-bb20-3e24ca72497c")
TERRAIN_X_MIN, TERRAIN_X_MAX = -360.0, 360.0
TERRAIN_Z_MIN, TERRAIN_Z_MAX = -160.0, 800.0
HAMLET_Z = 151.0
HAMLET_OFFSET = 53.0


def road_center(z):
    """Road centre in world X; its origin is exactly (0, 0)."""
    return 25.0 * math.sin(z / 143.0) + 10.0 * math.sin(z / 61.0)


def _smooth(a, b, value):
    t = max(0.0, min(1.0, (value - a) / (b - a)))
    return t * t * (3.0 - 2.0 * t)


def _lane_height(z):
    return 1.15 * math.sin(z / 83.0) + 0.38 * math.sin(z / 29.0)


def terrain_height(x, z):
    """Analytic source height shared by terrain, road and prop placement.

    The road corridor remains gentle (approximately -1.5..1.5 metres); hills
    rise outside it. The generated collision triangles sample this surface at
    4 m intervals. Gameplay should query the mesh collider for final grounding.
    """
    distance = abs(x - road_center(z))
    valley = _lane_height(z)
    hills = (3.2 * math.sin(x / 67.0 + 0.45 * math.sin(z / 91.0))
             + 2.6 * math.sin(z / 107.0)
             + 1.7 * math.sin((x + z) / 47.0))
    ridge = 31.0 * _smooth(88.0, 315.0, distance)
    ridge *= 0.68 + 0.32 * math.sin(z / 78.0 + x / 131.0) ** 2
    height = valley + _smooth(13.0, 66.0, distance) * (hills + ridge)
    # The small settlement occupies a gently graded terrace. Blend the edge
    # into the valley rather than balancing houses over individual hill peaks.
    hamlet_x = road_center(HAMLET_Z) + HAMLET_OFFSET
    radius = math.sqrt(((x - hamlet_x) / 67.0) ** 2 + ((z - HAMLET_Z) / 58.0) ** 2)
    terrace = _lane_height(HAMLET_Z) + 0.18
    height += (terrace - height) * (1.0 - _smooth(0.6, 1.1, radius))
    # Ensure the mounted actor's authored starting elevation is unambiguous.
    height *= _smooth(0.0, 22.0, math.hypot(x, z))
    return height


def _guid(path):
    return uuid.uuid5(_NAMESPACE, path).hex


def _reference(path):
    return {"Guid": _guid(path), "LocalID": 1}


def _write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")


def _meta(root, relative, importer, asset_type, settings=None):
    _write_json(root / "Assets" / (relative + ".meta"), {
        "Version": 1, "Guid": _guid(relative), "ImporterType": importer,
        "Path": relative, "ImportSettings": settings or {},
        "SubAssets": [{"Type": asset_type, "SourceKey": "main", "LocalID": 1,
                       "Name": Path(relative).stem}],
    })


def _unit(vector):
    length = math.sqrt(sum(value * value for value in vector))
    return tuple(value / length for value in vector) if length > 1e-12 else (0.0, 1.0, 0.0)


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def _subtract(a, b):
    return tuple(x - y for x, y in zip(a, b))


class _Mesh:
    """Small source-mesh builder; sections retain material-slot identities."""

    def __init__(self):
        self.positions, self.normals, self.uvs, self.colors = [], [], [], []
        self.sections = {}

    def vertex(self, position, normal, uv=(0.0, 0.0), color=(1.0, 1.0, 1.0, 1.0)):
        index = len(self.positions)
        self.positions.append(tuple(position))
        self.normals.append(_unit(normal))
        self.uvs.append(tuple(uv))
        self.colors.append(tuple(color))
        return index

    def triangle(self, a, b, c, slot=0):
        self.sections.setdefault(slot, []).extend((a, b, c))

    def face(self, points, slot=0, tile=1.0, color=(1.0, 1.0, 1.0, 1.0)):
        normal = _unit(_cross(_subtract(points[1], points[0]), _subtract(points[2], points[0])))
        dominant = max(range(3), key=lambda axis: abs(normal[axis]))
        axes = ((2, 1), (0, 2), (0, 1))[dominant]
        vertices = [self.vertex(point, normal, (point[axes[0]] / tile, point[axes[1]] / tile), color)
                    for point in points]
        for index in range(1, len(vertices) - 1):
            self.triangle(vertices[0], vertices[index], vertices[index + 1], slot)

    def box(self, center, size, slot=0, yaw=0.0, tile=1.0, color=(1.0, 1.0, 1.0, 1.0)):
        x, y, z = (value * 0.5 for value in size)
        faces = [((x, -y, z), (x, -y, -z), (x, y, -z), (x, y, z)),
                 ((-x, -y, -z), (-x, -y, z), (-x, y, z), (-x, y, -z)),
                 ((-x, -y, z), (x, -y, z), (x, y, z), (-x, y, z)),
                 ((x, -y, -z), (-x, -y, -z), (-x, y, -z), (x, y, -z)),
                 ((-x, y, z), (x, y, z), (x, y, -z), (-x, y, -z)),
                 ((-x, -y, -z), (x, -y, -z), (x, -y, z), (-x, -y, z))]
        cosine, sine = math.cos(yaw), math.sin(yaw)
        for face in faces:
            self.face([(center[0] + px * cosine + pz * sine, center[1] + py,
                        center[2] - px * sine + pz * cosine) for px, py, pz in face], slot, tile, color)

    def cylinder(self, center, radius, height, slot=0, segments=10, top_radius=None):
        upper = radius if top_radius is None else top_radius
        for index in range(segments):
            a, b = index * math.tau / segments, (index + 1) * math.tau / segments
            bottom_a = (center[0] + radius * math.cos(a), center[1], center[2] + radius * math.sin(a))
            bottom_b = (center[0] + radius * math.cos(b), center[1], center[2] + radius * math.sin(b))
            top_a = (center[0] + upper * math.cos(a), center[1] + height, center[2] + upper * math.sin(a))
            top_b = (center[0] + upper * math.cos(b), center[1] + height, center[2] + upper * math.sin(b))
            if upper > 0.0:
                self.face((bottom_a, top_a, top_b, bottom_b), slot)
                self.face(((center[0], center[1] + height, center[2]), top_b, top_a), slot)
            else:
                self.face((bottom_a, top_a, bottom_b), slot)
            self.face(((center[0], center[1], center[2]), bottom_a, bottom_b), slot)

    def branch(self, start, end, radius, tip_radius=None, slot=0, segments=7):
        """Tapered real geometry, usable for trunks, boughs and rough timbers."""
        direction = _unit(_subtract(end, start))
        if sum(value * value for value in _subtract(end, start)) < 1e-10:
            return
        side = _unit(_cross(direction, (0, 1, 0) if abs(direction[1]) < .92 else (1, 0, 0)))
        other = _cross(side, direction)
        tip_radius = radius * .35 if tip_radius is None else tip_radius
        rings = []
        for center, size in ((start, radius), (end, tip_radius)):
            rings.append([tuple(center[k] + size * (side[k] * math.cos(angle) + other[k] * math.sin(angle))
                                for k in range(3)) for angle in (math.tau * i / segments for i in range(segments))])
        for i in range(segments):
            j = (i + 1) % segments
            self.face((rings[0][i], rings[1][i], rings[1][j], rings[0][j]), slot, .65)
            self.face((start, rings[0][i], rings[0][j]), slot, .65)
            self.face((end, rings[1][j], rings[1][i]), slot, .65)

    def leaf(self, center, length, width, normal, angle, slot=0, color=(1, 1, 1, 1)):
        """Opaque, two-sided leaf geometry; no alpha shader or billboard needed."""
        normal = _unit(normal)
        side = _unit(_cross(normal, (0, 1, 0) if abs(normal[1]) < .92 else (1, 0, 0)))
        along = _cross(normal, side)
        u = tuple(side[k] * math.cos(angle) + along[k] * math.sin(angle) for k in range(3))
        v = tuple(-side[k] * math.sin(angle) + along[k] * math.cos(angle) for k in range(3))
        points = [tuple(center[k] + u[k] * a * width + v[k] * b * length for k in range(3))
                  for a, b in ((-.5, 0), (0, -.5), (.5, 0), (0, .5))]
        ids = [self.vertex(point, normal, uv, color) for point, uv in
               zip(points, ((0, .5), (.5, 0), (1, .5), (.5, 1)))]
        self.triangle(ids[0], ids[1], ids[2], slot)
        self.triangle(ids[0], ids[2], ids[3], slot)
        back = [self.vertex(point, tuple(-value for value in normal), uv, color) for point, uv in
                zip(points, ((0, .5), (.5, 0), (1, .5), (.5, 1)))]
        self.triangle(back[2], back[1], back[0], slot)
        self.triangle(back[3], back[2], back[0], slot)

    def ellipsoid(self, center, radii, slot=0, seed=1, roughness=0.12, rings=6, segments=11):
        rng = random.Random(seed)
        # One vertex per pole, not duplicate collapsed triangles.
        bottom = self.vertex((center[0], center[1] - radii[1], center[2]), (0, -1, 0))
        levels = []
        for row in range(1, rings):
            latitude = -math.pi / 2 + math.pi * row / rings
            level = []
            for column in range(segments):
                angle = math.tau * column / segments
                direction = (math.cos(latitude) * math.cos(angle), math.sin(latitude),
                             math.cos(latitude) * math.sin(angle))
                noise = 1.0 + rng.uniform(-roughness, roughness)
                point = tuple(center[axis] + direction[axis] * radii[axis] * noise for axis in range(3))
                normal = _unit(tuple(direction[axis] / radii[axis] for axis in range(3)))
                shade = rng.uniform(0.84, 1.0)
                level.append(self.vertex(point, normal, (column / segments * 3, row / rings * 3),
                                         (shade, shade, shade, 1)))
            levels.append(level)
        top = self.vertex((center[0], center[1] + radii[1], center[2]), (0, 1, 0))
        for column in range(segments):
            next_column = (column + 1) % segments
            self.triangle(bottom, levels[0][column], levels[0][next_column], slot)
            self.triangle(top, levels[-1][next_column], levels[-1][column], slot)
        for lower, upper in zip(levels, levels[1:]):
            for column in range(segments):
                next_column = (column + 1) % segments
                self.triangle(lower[column], upper[column], upper[next_column], slot)
                self.triangle(lower[column], upper[next_column], lower[next_column], slot)

    def write(self, root, name, folder="Environment/Meshes"):
        relative = folder + "/" + name + ".mesh"
        path = root / "Assets" / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        indices, sections = [], []
        for slot, values in sorted(self.sections.items()):
            sections.append((len(indices), len(values), slot))
            indices.extend(values)
        encoded = name.encode("utf-8")
        data = bytearray(b"GSMESH02" + struct.pack("<6I", 2, 13, len(self.positions), len(indices),
                                                 len(sections), len(encoded)))
        data.extend(encoded)
        data.extend(b"\0" * (-len(data) % 4))
        for values in (self.positions, self.normals, self.uvs, self.colors):
            for value in values:
                data.extend(struct.pack("<" + "f" * len(value), *value))
        data.extend(struct.pack("<" + "I" * len(indices), *indices))
        for section in sections:
            data.extend(struct.pack("<3I", *section))
        path.write_bytes(data)
        _meta(root, relative, "Mesh", 3, {"NormalsMode": "Import", "ScaleFactor": 1,
                                         "MeshCompression": "Off", "GenerateLightmapUVs": False})
        return _reference(relative)


def _png(path, pixels, size):
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    rows = b"".join(b"\0" + pixels[y * size * 4:(y + 1) * size * 4] for y in range(size))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">2I5B", size, size, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def _tile_noise(size, frequency, rng):
    grid = [[rng.random() for _ in range(frequency)] for _ in range(frequency)]
    samples = []
    for pixel in range(size):
        cell, fraction = divmod(pixel * frequency, size)
        t = fraction / size
        samples.append((cell, (cell + 1) % frequency, t * t * (3 - 2 * t)))
    for y0, y1, fy in samples:
        for x0, x1, fx in samples:
            a = grid[y0][x0] + (grid[y0][x1] - grid[y0][x0]) * fx
            b = grid[y1][x0] + (grid[y1][x1] - grid[y1][x0]) * fx
            yield a + (b - a) * fy - .5


def _texture(root, name, base, kind):
    """Generate seamless color and optional micro-relief textures."""
    size = 256
    rng = random.Random(603 + sum(name.encode("utf-8")))
    pixels = bytearray()
    heights = []
    layers = (_tile_noise(size, frequency, rng) for frequency in (5, 13, 35, 89))
    encode = lambda value: round(max(0, min(255, value)))
    phase = math.tau / size
    for index, (broad, patch, fine, grit) in enumerate(zip(*layers)):
        y, x = divmod(index, size)
        tint = (0, 0, 0)
        height = .28 * patch + .46 * fine + .14 * grit
        shift = 15 * broad + 13 * patch + 10 * fine + 8 * grit
        if kind == "grass":
            fibre = math.sin(phase * (67 * x + 17 * y) + 22 * patch + 18 * fine)
            shift += 3 * fibre + 4 * grit
            warm = max(0.0, broad + patch * .65 - .09)
            tint = (37 * warm, 7 * warm, -13 * warm)
        elif kind == "dirt":
            stone = _smooth(.37, .57, fine + grit * .42)
            shift += 25 * stone
            height += .28 * stone
            tint = (7 * broad - 8 * stone, 3 * broad, 4 * stone)
        elif kind == "stone":
            row, mortar_y = divmod(y + 16, 32)
            column, mortar_x = divmod(x + (row % 2) * 32, 64)
            edge = min(mortar_x, 63 - mortar_x, mortar_y, 31 - mortar_y)
            bevel = _smooth(0.5, 4.5, edge + 2.6 * fine)
            height += .6 * bevel
            shift += -25 * (1 - bevel) + ((row % 8 * 37 + (column % 4) * 13) % 25 - 12) * bevel
        elif kind == "wood":
            fibre = math.sin(phase * (43 * x) + 3.3 * math.sin(phase * y) + 5 * fine)
            split = _smooth(0, 2, min(x % 64, 63 - x % 64))
            shift += 10 * fibre - 22 * (1 - split)
            height += .12 * fibre + .36 * split
        elif kind == "roof":
            row, overlap = divmod(y, 32)
            gap = (x + row % 2 * 16) % 32
            tile = _smooth(0, 4, min(overlap, gap, 31 - gap) + fine)
            shift -= 25 * (1 - tile)
            height += .52 * tile
        pixels.extend(encode(channel + shift + offset) for channel, offset in zip(base, tint))
        pixels.append(255)
        heights.append(height)
    relative = "Environment/Textures/" + name + ".png"
    _png(root / "Assets" / relative, bytes(pixels), size)
    _meta(root, relative, "Texture", 1, {"ColorSpace": "SRGB", "AlphaSource": "None",
        "GenerateMipmaps": True, "FilterMode": "Trilinear", "WrapU": "Repeat", "WrapV": "Repeat", "MaxSize": 256})
    normal = None
    if kind in ("dirt", "stone", "wood", "roof"):
        normals = bytearray()
        for y in range(size):
            for x in range(size):
                dx = heights[y * size + (x + 1) % size] - heights[y * size + (x - 1) % size]
                dy = heights[((y + 1) % size) * size + x] - heights[((y - 1) % size) * size + x]
                direction = _unit((-.75 * dx, -.75 * dy, 1))
                normals.extend(encode((component * .5 + .5) * 255) for component in direction)
                normals.append(255)
        normal_path = "Environment/Textures/" + name + "Normal.png"
        _png(root / "Assets" / normal_path, bytes(normals), size)
        _meta(root, normal_path, "Texture", 1, {"ColorSpace": "Linear", "AlphaSource": "None",
            "GenerateMipmaps": True, "FilterMode": "Trilinear", "WrapU": "Repeat", "WrapV": "Repeat", "MaxSize": 256})
        normal = _reference(normal_path)
    return _reference(relative), normal


def _material(root, name, color, texture=None, normal=None, roughness=0.9):
    relative = "Environment/Materials/" + name + ".mat"
    material = {"Version": 1, "Name": name, "Shader": "GameSprout/Lit",
        "BaseColor": list(color) + [1], "BaseColorTexture": texture, "BaseColorTextureST": [1, 1, 0, 0],
        "Metallic": 0, "Roughness": roughness, "DoubleSided": False, "EnableGPUInstancing": True,
        "SpecularHighlights": True, "Stylization": 0}
    if normal:
        material.update({"NormalTexture": normal, "NormalTextureST": [1, 1, 0, 0], "NormalScale": .7})
    _write_json(root / "Assets" / relative, material)
    _meta(root, relative, "Material", 5)
    return _reference(relative)


def _render(mesh, materials):
    return [{"Type": "GameSproutRuntime.Mesh", "Mesh": mesh},
            {"Type": "GameSproutRuntime.MeshRenderer", "Visible": True, "Materials": list(materials)}]


def _box_collider(center, size):
    return {"Type": "GameSproutRuntime.BoxCollider", "Settings": {"Center": list(center)},
            "Shape": {"Size": list(size)}}


def _yaw(angle):
    return [0, math.sin(angle / 2), 0, math.cos(angle / 2)]


def _surface_normal(x, z):
    return _unit((terrain_height(x - 0.15, z) - terrain_height(x + 0.15, z), 0.3,
                  terrain_height(x, z - 0.15) - terrain_height(x, z + 0.15)))


def _ground_height(x, z):
    """Height of the four-metre terrain triangles under a visual detail."""
    x0 = TERRAIN_X_MIN + math.floor((x - TERRAIN_X_MIN) / 4) * 4
    z0 = TERRAIN_Z_MIN + math.floor((z - TERRAIN_Z_MIN) / 4) * 4
    u, v = (x - x0) / 4, (z - z0) / 4
    a, b = terrain_height(x0, z0), terrain_height(x0 + 4, z0 + 4)
    if u < v:
        return a * (1 - v) + terrain_height(x0, z0 + 4) * (v - u) + b * u
    return a * (1 - u) + terrain_height(x0 + 4, z0) * (u - v) + b * v


def _meadow_tint(x, z, normal):
    field = .5 + .5 * math.sin(x * .049 + .62 * math.sin(z * .038)) * math.cos(z * .053 - x * .019)
    scrub = .5 + .5 * math.cos(x * .193 - .83 * math.sin(z * .107)) * math.sin(z * .113 + x * .062)
    dry = .63 * field + .37 * scrub
    steep = 1.0 - _smooth(.74, .97, normal[1])
    bare = .65 * dry + .35 * steep
    return (.70 + .28 * dry + .14 * steep, .74 + .19 * dry, .69 + .17 * dry - .20 * bare, 1)


def _terrain(root, node, parent, material):
    # Twenty tiles allow culling and small, independently cooked collision meshes.
    for tz in range(5):
        for tx in range(4):
            mesh = _Mesh()
            x0, z0 = TERRAIN_X_MIN + tx * 180.0, TERRAIN_Z_MIN + tz * 192.0
            nx, nz, step = 45, 48, 4.0
            for iz in range(nz + 1):
                z = z0 + iz * step
                for ix in range(nx + 1):
                    x = x0 + ix * step
                    height, normal = terrain_height(x, z), _surface_normal(x, z)
                    color = _meadow_tint(x, z, normal)
                    mesh.vertex((x, height, z), normal, (x / 4.5, z / 4.5), color)
            for iz in range(nz):
                for ix in range(nx):
                    a = iz * (nx + 1) + ix
                    b = a + nx + 1
                    mesh.triangle(a, b, b + 1)
                    mesh.triangle(a, b + 1, a + 1)
            reference = mesh.write(root, "Valley_%02d_%02d" % (tx, tz))
            components = _render(reference, [material]) + [{"Type": "GameSproutRuntime.MeshCollider",
                "Shape": {"Mesh": reference, "Convex": False}}]
            node("Valley ground %d.%d" % (tx + 1, tz + 1), parent=parent, components=components, static=True)


def _road_edge(z, side):
    return (2.75 + .36 * math.sin(z * .079 + side * .7)
            + .24 * math.sin(z * .37 + side * 2.1)
            + .20 * math.sin(z * 1.57 + side * 1.7) + .12 * math.sin(z * 4.11 + side))


def _road(root, node, parent, material):
    mesh = _Mesh()
    # Fine, irregular shoulders around the camera ease into wider samples beyond the hamlet.
    rows = list(range(-130, -24, 2)) + [i * .5 for i in range(-48, 382)] + list(range(191, 801, 2))
    widths = (-1, -.89, -.74, -.54, -.34, -.14, 0, .14, .34, .54, .74, .89, 1)
    columns = len(widths)
    for z in rows:
        center = road_center(z)
        for width in widths:
            x = center + width * _road_edge(z, 1 if width > 0 else -1)
            tracks = (1 - _smooth(.04, .26, abs(abs(width) - .51)))
            shoulder = _smooth(.66, 1, abs(width))
            blemish = .5 + .5 * math.sin(z * .59 + width * 7.3) * math.sin(z * .23 - width * 5.6)
            tint = .96 - .09 * tracks - .10 * shoulder - .09 * blemish
            color = (tint, tint * (1 + .04 * shoulder), tint * (.96 - .10 * shoulder), 1)
            mesh.vertex((x, _ground_height(x, z) + .047, z), _surface_normal(x, z), (x / 2.8, z / 2.8), color)
    for row in range(len(rows) - 1):
        for column in range(columns - 1):
            a = row * columns + column
            b = a + columns
            mesh.triangle(a, b, b + 1)
            mesh.triangle(a, b + 1, a + 1)
    road = _render(mesh.write(root, "ValleyRoad"), [material])
    road[1]["ShadowCastingMode"] = "Off"
    node("The king's road", parent=parent, components=road, static=True)
    # A curved approach into the small settlement, leaving the main road open.
    branch = _Mesh()
    for row in range(61):
        t = row / 60
        x = road_center(HAMLET_Z - 36) + (45 * t)
        z = HAMLET_Z - 36 + 51 * t + 8 * math.sin(t * math.pi)
        for side in range(5):
            xx = x + (side / 4 - 0.5) * 5.0
            branch.vertex((xx, _ground_height(xx, z) + .073, z), _surface_normal(xx, z), (xx / 2.8, z / 2.8))
    for row in range(60):
        for column in range(4):
            a = row * 5 + column
            branch.triangle(a, a + 5, a + 6)
            branch.triangle(a, a + 6, a + 1)
    lane = _render(branch.write(root, "HamletLane"), [material])
    lane[1]["ShadowCastingMode"] = "Off"
    node("Hamlet lane", parent=parent, components=lane, static=True)


def _house(variant):
    mesh = _Mesh()
    rng = random.Random(611 + variant)
    width, length, wall = ((7.2, 10.0, 3.0), (6.6, 8.6, 2.75), (9.2, 13.0, 3.5))[variant]
    # Slot order: pale lime plaster, oak, stone foundation, weathered roof.
    mesh.box((0, .23, 0), (width + .3, .46, length + .3), 2, tile=1.7)
    mesh.box((0, wall / 2 + .4, 0), (width, wall, length), 0, tile=2.5)
    # Distinct stones break the plinth's silhouette without a grid of identical cubes.
    for side in (-1, 1):
        for index in range(int(length / .65)):
            z = -length / 2 + .4 + index * .65
            mesh.box((side * (width / 2 + .04), .25 + rng.uniform(-.03, .03), z),
                     (.23, rng.uniform(.3, .47), rng.uniform(.5, .64)), 2,
                     yaw=rng.uniform(-.05, .05), tile=1.7)
    eave, peak, reach = wall + .4, wall + 2.85, width / 2 + .58
    front, back = -length / 2 - .55, length / 2 + .55
    mesh.face(((-reach, eave, back), (0, peak, back), (0, peak, front), (-reach, eave, front)), 3, 2)
    mesh.face(((0, peak, back), (reach, eave, back), (reach, eave, front), (0, peak, front)), 3, 2)
    mesh.face(((-width / 2, eave, length / 2), (width / 2, eave, length / 2), (0, peak - .2, length / 2)), 0, 2)
    mesh.face(((width / 2, eave, -length / 2), (-width / 2, eave, -length / 2), (0, peak - .2, -length / 2)), 0, 2)
    # Overlapping shingle rows and uneven eaves catch real directional shadows.
    for side in (-1, 1):
        for row in range(10):
            t0, t1 = row / 10, min(1, (row + 1.15) / 10)
            for column in range(int((length + 1.1) / .48)):
                z0 = front + column * .48 + (row % 2) * .17
                z1 = min(back, z0 + .46)
                if z1 <= z0:
                    continue
                x0, x1 = side * reach * t0, side * reach * t1
                y0, y1 = peak - (peak - eave) * t0, peak - (peak - eave) * t1
                offset = .025 + rng.uniform(0, .018)
                points = ((x0, y0 + offset, z0), (x1, y1 + offset, z0),
                          (x1, y1 + offset, z1), (x0, y0 + offset, z1))
                if side > 0:
                    points = tuple(reversed(points))
                mesh.face(points, 3, 2, (rng.uniform(.79, 1),) * 3 + (1,))
        mesh.branch((side * reach, eave - .02, front), (side * reach, eave - .02, back), .1, .09, 1)
    mesh.branch((0, peak + .035, front - .08), (0, peak + .035, back + .08), .1, .09, 1)
    # Timber framing, braced panels and shuttered openings.
    for x in (-width / 2 - .04, width / 2 + .04):
        for z in (-length / 2 + .12, 0, length / 2 - .12):
            mesh.box((x, eave / 2, z), (.22, eave, .24), 1)
        for y in (.65, eave - .1):
            mesh.box((x, y, 0), (.22, .18, length + .25), 1)
        for z0, z1 in ((-length / 2 + .2, -.35), (.35, length / 2 - .2)):
            mesh.branch((x, .75, z0), (x, eave - .3, z1), .095, .08, 1, 4)
        for z in (-length * .27, length * .27):
            mesh.box((x * 1.012, 1.95, z), (.1, .94, .82), 1, color=(.15, .15, .15, 1))
            for zz in (z - .48, z + .48):
                mesh.box((x * 1.027, 1.95, zz), (.13, 1.08, .09), 1)
            for y in (1.45, 2.44):
                mesh.box((x * 1.027, y, z), (.14, .11, 1.08), 1)
            mesh.box((x * 1.045, 1.38, z), (.45, .12, 1.2), 2)
            mesh.box((x * 1.035, 1.94, z + .76), (.11, .93, .43), 1)
            mesh.box((x * 1.035, 1.94, z - .76), (.11, .93, .43), 1)
    for z in (-length / 2 - .045, length / 2 + .045):
        for x in (-width / 2 + .12, width / 2 - .12):
            mesh.box((x, eave / 2, z), (.24, eave, .2), 1)
        mesh.box((0, eave - .1, z), (width, .23, .2), 1)
        mesh.box((0, 1.42, z * 1.008), (1.13, 2.05, .12), 1, color=(.55, .55, .55, 1))
        for x in (-.65, .65):
            mesh.box((x, 1.44, z * 1.021), (.12, 2.2, .2), 1)
        for y in (.83, 1.93):
            mesh.box((0, y, z * 1.024), (1.02, .09, .13), 1)
        mesh.box((0, .32, z * 1.08), (1.6, .28, .76), 2)
        mesh.branch((-width / 2, eave, z), (0, peak - .2, z), .08, .08, 1, 4)
        mesh.branch((width / 2, eave, z), (0, peak - .2, z), .08, .08, 1, 4)
    mesh.box((width * .23, eave + 1.55, length * .18), (.75, 2.65, .9), 2, tile=1.7)
    mesh.box((width * .23, eave + 2.94, length * .18), (.97, .16, 1.12), 2, tile=1.7)
    return mesh, width, length, eave


def _conifer(variant, detailed=True):
    mesh = _Mesh()
    rng = random.Random(451 + variant)
    mesh.branch((0, 0, 0), (.17, 11.8, -.1), .3, .035, 0, 9)
    tiers, sides = (9, 5) if detailed else (7, 4)
    for tier in range(tiers):
        fraction = tier / (tiers - 1)
        y = 3.2 + fraction * 7.3
        for side in range(sides):
            angle = side * math.tau / sides + tier * 1.71 + rng.uniform(-.24, .24)
            reach = (3.5 - fraction * 2.8) * rng.uniform(.78, 1.16)
            end = (reach * math.cos(angle), y + rng.uniform(-.35, .6), reach * math.sin(angle))
            if detailed:
                mesh.branch((.02, y, 0), end, .085 - fraction * .045, .015, 0, 5)
            for _ in range(13 if detailed else 7):
                t = rng.uniform(.19, 1.03)
                fringe = .77 * (1.15 - t)
                center = (end[0] * t + rng.uniform(-fringe, fringe), end[1] + rng.uniform(-.38, .54),
                          end[2] * t + rng.uniform(-fringe, fringe))
                shade = rng.uniform(.61, 1.04)
                mesh.leaf(center, rng.uniform(.67, 1.36), rng.uniform(.35, .7),
                          (rng.uniform(-.55, .55), 1, rng.uniform(-.55, .55)), angle + rng.uniform(-.8, .8),
                          1, (shade, shade, shade * .91, 1))
    _leaf_cloud(mesh, (.17, 11.3, -.1), (.55, 1.1, .55), rng, 1, 46 if detailed else 20, .44)
    return mesh


def _broadleaf(variant, detailed=True):
    mesh = _Mesh()
    rng = random.Random(8831 + variant)
    mesh.branch((0, -.15, 0), (.28, 4.4, -.15), .46, .27, 0, 10)
    for i in range(8 if detailed else 7):
        angle = i * 2.399 + variant
        reach = rng.uniform(2.3, 4.0)
        base = (.25, rng.uniform(2.9, 4.25), -.13)
        fork = (math.cos(angle) * reach * .67, rng.uniform(5.0, 6.7), math.sin(angle) * reach * .67)
        mesh.branch(base, fork, .2, .08, 0, 6)
        for offset in ((-.32, .32) if detailed else (0,)):
            a = angle + offset
            end = (math.cos(a) * reach, fork[1] + rng.uniform(.75, 1.8), math.sin(a) * reach)
            if detailed:
                mesh.branch(fork, end, .08, .017, 0, 5)
            pad = (rng.uniform(1.3, 1.72), rng.uniform(.83, 1.32), rng.uniform(1.18, 1.65))
            mesh.ellipsoid(end, tuple(axis * .70 for axis in pad), 1, seed=rng.randrange(10000),
                           roughness=.17, rings=4, segments=7)
            _leaf_cloud(mesh, end, pad, rng, 1, 29 if detailed else 10, .58 if detailed else .77)
    _leaf_cloud(mesh, (.3, 8.0, -.1), (2.05, 1.45, 1.7), rng, 1, 70 if detailed else 25, .64)
    return mesh


def _leaf_cloud(mesh, center, size, rng, slot, count, leaf_size):
    for _ in range(count):
        azimuth, vertical = rng.uniform(0, math.tau), rng.uniform(-1, 1)
        radius = rng.random() ** .4
        radial = math.sqrt(max(0, 1 - vertical * vertical))
        direction = (radial * math.cos(azimuth), vertical, radial * math.sin(azimuth))
        point = tuple(center[k] + direction[k] * size[k] * radius for k in range(3))
        shade = rng.uniform(.67, 1.07)
        mesh.leaf(point, leaf_size * rng.uniform(.7, 1.4), leaf_size * rng.uniform(.45, .85),
                  (direction[0] * .65, .6 + direction[1] * .4, direction[2] * .65),
                  rng.uniform(0, math.tau), slot, (shade, shade, shade * rng.uniform(.79, 1.06), 1))


def _ground_cover(root, node, parent, materials):
    """Dense close-view detail in small cullable patches, never gameplay collision."""
    rng = random.Random(574411)
    for station in range(13):
        z0 = -20 + station * 16
        for side in (-1, 1):
            mesh = _Mesh()
            for clump in range(39):
                center_z = z0 + rng.uniform(.7, 15.3)
                distance = rng.uniform(.05, 1.0) if clump < 24 else rng.uniform(1.0, 11)
                dry = (math.sin(center_z * .22 + side) + math.sin(distance * .71)) > .43
                for tuft in range(rng.randint(2, 6)):
                    z = center_z + rng.uniform(-.7, .7)
                    across = _road_edge(z, side) + distance + rng.uniform(-.32, .32)
                    x = road_center(z) + side * across
                    base = _ground_height(x, z)
                    if across < _road_edge(z, side):
                        base += .045
                    height = rng.uniform(.12, .43) * (.7 if distance < .65 else 1.0)
                    for blade in range(3):
                        angle = rng.uniform(0, math.tau)
                        xx, zz = x + rng.uniform(-.13, .13), z + rng.uniform(-.13, .13)
                        shade = rng.uniform(.72, 1.03)
                        mesh.leaf((xx, base + height * .46, zz), height, rng.uniform(.023, .080),
                                  (math.cos(angle), .2, math.sin(angle)), rng.uniform(-.31, .31),
                                  int(dry and blade != 0), (shade, shade * rng.uniform(.97, 1.04), shade * .88, 1))
                    if dry and tuft % 5 == 0:
                        angle = rng.uniform(0, math.tau)
                        mesh.leaf((x, base + height * .51, z), height * 1.23, .030,
                                  (math.cos(angle), .09, math.sin(angle)), .17,
                                  1, (1, .94, .77, 1))
                        mesh.leaf((x + .018, base + height * 1.03, z), .095, .049,
                                  (math.cos(angle), .24, math.sin(angle)), .35,
                                  1, (1, .92, .72, 1))
            reference = mesh.write(root, "RoadsideHerbage_%02d_%s" % (station, "L" if side < 0 else "R"))
            blades = _render(reference, [materials["MeadowGrass"], materials.get("DryGrass", materials["FieldEarth"])])
            blades[1]["ShadowCastingMode"] = "Off"
            node("Road verge — grasses and seed heads", parent=parent,
                 components=blades, static=True)
    # Low hazel/bramble thickets give the road edge an intermediate scale.
    shrub = _Mesh()
    for i in range(7):
        angle = i * 2.399
        end = (math.cos(angle) * .7, rng.uniform(.55, 1.15), math.sin(angle) * .7)
        shrub.branch((0, -.05, 0), end, .035, .008, 0, 5)
        _leaf_cloud(shrub, end, (.65, .55, .6), rng, 1, 27, .27)
    shrub_reference = shrub.write(root, "HazelBramble")
    for center_z, center_x in ((10, -9.3), (23, 9.1), (42, -9.4), (55, 8.6),
                               (80, -13), (103, 10.5), (128, -12), (159, 12)):
        for _ in range(rng.randint(3, 5)):
            z = center_z + rng.uniform(-4.6, 4.6)
            x = road_center(z) + center_x + rng.uniform(-1.9, 1.9)
            size = rng.uniform(.67, 1.35)
            node("Hazel and bramble verge", parent=parent, position=[x, _ground_height(x, z), z],
                 scale=[size] * 3, rotation=_yaw(rng.uniform(0, math.tau)),
                 components=_render(shrub_reference, [materials["WeatheredOak"], materials["LeafGreen"]]), static=True)
    # Broken soil around the shoulders, and flint along the compacted wheel tracks.
    for chunk in range(9):
        mesh = _Mesh()
        for _ in range(25):
            z = chunk * 20 + rng.uniform(-10, 10)
            side = rng.choice((-1, 1))
            x = road_center(z) + side * (_road_edge(z, side) + rng.uniform(.05, 2.9))
            length, width = rng.uniform(.25, .94), rng.uniform(.18, .61)
            tone = rng.uniform(.77, 1.04)
            center = mesh.vertex((x, _ground_height(x, z) + .019, z), _surface_normal(x, z),
                                 (x / 2.8, z / 2.8), (tone, tone, tone, 1))
            ring = []
            for i in range(7):
                angle, size = math.tau * i / 7, rng.uniform(.58, 1.05)
                px, pz = x + width * math.cos(angle) * size, z + length * math.sin(angle) * size
                ring.append(mesh.vertex((px, _ground_height(px, pz) + .019, pz), _surface_normal(px, pz),
                                        (px / 2.8, pz / 2.8), (tone, tone, tone, 1)))
            for i, vertex in enumerate(ring):
                mesh.triangle(center, ring[(i + 1) % len(ring)], vertex)
        for _ in range(58):
            z = chunk * 20 + rng.uniform(-10, 10)
            side = rng.choice((-1, 1))
            distance = _road_edge(z, side) + rng.uniform(-1.65, 1.7)
            x = road_center(z) + side * distance
            size = rng.uniform(.023, .102)
            height = _ground_height(x, z) + (.045 if distance < _road_edge(z, side) else 0)
            mesh.ellipsoid((x, height + size * .17, z), (size, size * .38, size * .79),
                           1, seed=rng.randrange(100000), roughness=.17, rings=3, segments=5)
        verge = _render(mesh.write(root, "RoadsideFlint_%d" % chunk),
                        [materials["PackedEarth"], materials["RockSurface"]])
        verge[1]["ShadowCastingMode"] = "Off"
        node("Disturbed roadside soil and flint", parent=parent, components=verge, static=True)


def _dry_wall(seed):
    rng = random.Random(seed)
    mesh = _Mesh()
    for row in range(3):
        for column in range(8):
            x = column * .51 - 1.78 + (row % 2) * .12
            mesh.ellipsoid((x, .16 + row * .25 + rng.uniform(-.04, .04), rng.uniform(-.08, .08)),
                           (rng.uniform(.25, .32), rng.uniform(.15, .19), rng.uniform(.25, .34)),
                           seed=rng.randrange(10000), roughness=.13, rings=3, segments=6)
    return mesh


def _foreground_setting(root, node, parent, materials, houses):
    stone, timber = materials["Limestone"], materials["WeatheredOak"]
    walls = [_dry_wall(413 + i).write(root, "DryStoneBoundary_%d" % i) for i in range(2)]
    for index in range(18):
        z = 8 + index * 4.1
        if index in (7, 8, 15):
            continue
        x = min(-8.0, road_center(z) - 10.5)
        angle = -.5 * math.pi + .07 * math.sin(index * .7)
        node("Old field boundary", parent=parent, position=[x, terrain_height(x, z) - .08, z],
             rotation=_yaw(angle), components=_render(walls[index % 2], [stone]) +
             [_box_collider((0, .37, 0), (4.05, .75, .65))], static=True)
    # A modest roadside croft is readable well before the distant settlement.
    reference, width, length, height = houses[1]
    x, z = road_center(61) + 20, 61
    node("Roadkeeper's croft", parent=parent, position=[x, terrain_height(x, z) - .12, z],
         rotation=_yaw(-.2), components=_render(reference, [materials["LimePlaster"], timber, stone, materials["OldRoof"]]) +
         [_box_collider((0, height / 2, 0), (width, height, length))], static=True)
    # Broken field-stone foundations rather than a toy-sized decorative castle.
    ruin = _Mesh()
    rng = random.Random(981)
    for side in (-1, 1):
        for i in range(13):
            if side == 1 and i in (5, 6, 7):
                continue
            height = .48 + .25 * math.sin(i * .7 + side) ** 2
            ruin.box((side * 3.6, height * .5, i * .56 - 3.3), (.65, height, .53), 0, tile=1.5)
    ruin.box((0, .75, 3.65), (7.8, 1.5, .8), 0, tile=1.5)
    for i in range(38):
        x = rng.uniform(-4.4, 4.4)
        z = rng.uniform(-4.1, 4.1)
        size = rng.uniform(.14, .36)
        ruin.ellipsoid((x, size * .32, z), (size, size * .55, size * .72),
                       seed=i + 71, roughness=.19, rings=3, segments=6)
    x, z = -23, 51
    node("Abandoned shepherd's footing", parent=parent, position=[x, terrain_height(x, z) - .05, z],
         rotation=_yaw(.24), components=_render(ruin.write(root, "ShepherdsFooting"), [stone]), static=True)
    # A barrel and cut logs tell a rural working setting without extra shaders.
    stores = _Mesh()
    for i in range(5):
        start = (-1.3 + i * .48, .22, -.7)
        end = (-1.3 + i * .48, .22, 1.2 + rng.uniform(-.1, .1))
        stores.branch(start, end, .22, .19, 0, 9)
    for i in range(3):
        stores.branch((-.8 + i * .48, .58, -.7), (-.8 + i * .48, .58, 1.18), .2, .18, 0, 9)
    stores.cylinder((2.1, 0, 0), .36, .88, 0, 12)
    for y in (.16, .7):
        stores.cylinder((2.1, y, 0), .375, .07, 1, 12)
    node("Croft firewood and water barrel", parent=parent,
         position=[road_center(61) + 15, terrain_height(road_center(61) + 15, 62), 62],
         components=_render(stores.write(root, "CroftStores"), [timber, stone]), static=True)


def _gate():
    mesh = _Mesh()
    for x in (-6.5, 6.5):
        mesh.box((x, 4.25, 0), (3, 8.5, 2.6), 0, tile=1.7)
        mesh.box((x, .45, 0), (3.5, .9, 3.1), 0, tile=1.7)
        mesh.box((x, 8.7, 0), (3.5, .55, 3.1), 0, tile=1.7)
        mesh.box((x, 9.25, 0), (2.4, .65, 1.8), 0, tile=1.7)
    # Voussoirs form a genuine open arch; the riding opening stays 10 m wide.
    for i in range(13):
        a, b = math.pi * i / 13, math.pi * (i + 1) / 13
        inner, outer, cy = 5.0, 6.0, 3.65
        outline = [(inner * math.cos(a), cy + inner * math.sin(a)),
                   (outer * math.cos(a), cy + outer * math.sin(a)),
                   (outer * math.cos(b), cy + outer * math.sin(b)),
                   (inner * math.cos(b), cy + inner * math.sin(b))]
        front = [(x, y, 1.31) for x, y in outline]
        back = [(x, y, -1.31) for x, y in outline]
        mesh.face(front, 0, 1.7)
        mesh.face(tuple(reversed(back)), 0, 1.7)
        for j in range(4):
            k = (j + 1) % 4
            mesh.face((front[j], back[j], back[k], front[k]), 0, 1.7)
    return mesh


def _tower():
    mesh = _Mesh()
    mesh.cylinder((0, 0, 0), 3.8, .7, 0, 12)
    mesh.cylinder((0, .7, 0), 3.35, 10.5, 0, 12)
    mesh.cylinder((0, 10.6, 0), 3.7, .6, 0, 12)
    for i in range(10):
        angle = math.tau * i / 10
        mesh.box((3.1 * math.sin(angle), 11.7, 3.1 * math.cos(angle)), (1.2, 1.3, 1.0), 0, angle, 1.7)
    for z in (-3.33, 3.33):
        for y in (4.5, 8.0):
            mesh.box((0, y, z), (.35, 1.5, .12), 1)
    mesh.box((0, 1.45, -3.34), (1.5, 2.5, .15), 1)
    return mesh


def build_landscape(root, node):
    """Write generated source assets and append scenery using ``node``.

    ``node(name, parent=0, position=None, scale=None, components=(),
    rotation=None, static=False)`` must return a dict with ``ID``. All scenery
    is static. Source identities are stable across repeated builds. The return
    value provides bounds and useful named scene identities to the host script.
    """
    root = Path(root)
    environment = node("Countryside — Woldmere valley", static=True)["ID"]
    terrain_group = node("Ground and lanes", parent=environment, static=True)["ID"]
    buildings = node("Woldmere hamlet", parent=environment, static=True)["ID"]
    woodland = node("Woodland and rock outcrops", parent=environment, static=True)["ID"]
    landmarks = node("Old north road gate", parent=environment, static=True)["ID"]

    textures = {name: _texture(root, name, base, kind) for name, base, kind in (
        ("MeadowGrass", (119, 134, 76), "grass"), ("PackedEarth", (151, 129, 92), "dirt"),
        ("Limestone", (155, 150, 137), "stone"), ("WeatheredOak", (99, 76, 54), "wood"),
        ("LimePlaster", (194, 177, 143), "plaster"), ("OldRoof", (124, 98, 76), "roof"),
        ("LeafGreen", (98, 127, 68), "grass"), ("PineGreen", (74, 104, 73), "grass"),
        ("DryGrass", (167, 147, 88), "grass"), ("FieldEarth", (115, 92, 62), "dirt"),
        ("RockSurface", (138, 138, 124), "plaster"))}
    materials = {name: _material(root, name, (1, 1, 1), texture, normal)
                 for name, (texture, normal) in textures.items()}
    timber = materials["WeatheredOak"]
    stone = materials["Limestone"]
    _terrain(root, node, terrain_group, materials["MeadowGrass"])
    _road(root, node, terrain_group, materials["PackedEarth"])

    house_materials = [materials["LimePlaster"], timber, stone, materials["OldRoof"]]
    house_shapes = []
    for variant in range(3):
        mesh, width, length, height = _house(variant)
        house_shapes.append((mesh.write(root, "CroftHouse_%d" % variant), width, length, height))
    village_x = road_center(HAMLET_Z) + HAMLET_OFFSET
    for i, (dx, dz, yaw, variant) in enumerate(((-5, -17, -.25, 0), (21, -6, -1.3, 1),
                                               (-8, 21, .3, 1), (25, 25, 1.4, 2), (7, 43, -.1, 0))):
        x, z = village_x + dx, HAMLET_Z + dz
        reference, width, length, height = house_shapes[variant]
        node("Croft %d" % (i + 1), parent=buildings, position=[x, terrain_height(x, z) - .12, z],
             rotation=_yaw(yaw), components=_render(reference, house_materials) +
             [_box_collider((0, height / 2, 0), (width, height, length))], static=True)

    fence_mesh = _Mesh()
    for x in (-2, 2):
        fence_mesh.box((x, .75, 0), (.22, 1.5, .24), 0)
    for y in (.6, 1.2):
        fence_mesh.box((0, y, 0), (4.15, .14, .14), 0)
    fence = fence_mesh.write(root, "SplitRailFence")
    for side in (-1, 1):
        x = village_x + side * 36
        for index in range(18):
            z = HAMLET_Z - 35 + index * 4
            if side < 0 and HAMLET_Z - 21 <= z <= HAMLET_Z + 5:
                continue  # The curved approach crosses this opening.
            node("Paddock rail", parent=buildings, position=[x, terrain_height(x, z), z],
                 rotation=_yaw(math.pi / 2), components=_render(fence, [timber]) +
                 [_box_collider((0, .8, 0), (4.1, 1.6, .25))], static=True)
    for edge in (-35, 37):
        z = HAMLET_Z + edge
        for index in range(18):
            x = village_x - 34 + index * 4
            if edge < 0 and -13 < x - village_x < 2:
                continue  # Hamlet approach remains open.
            node("Paddock rail", parent=buildings, position=[x, terrain_height(x, z), z],
                 components=_render(fence, [timber]) + [_box_collider((0, .8, 0), (4.1, 1.6, .25))], static=True)

    # Low terraced strips and furrows outside the paddock add a worked landscape.
    for index, (cx, cz) in enumerate(((village_x + 49, HAMLET_Z - 10),
                                     (village_x - 6, HAMLET_Z + 65))):
        field_mesh = _Mesh()
        for row in range(25):
            for column in range(35):
                x, z = cx + column * .675 - 11.475, cz + row - 12
                height = terrain_height(x, z) + .05 + (column % 2) * .11
                shade = .88 if column % 2 else 1
                field_mesh.vertex((x, height, z), _surface_normal(x, z), (x / 2, z / 2),
                                  (shade, shade, shade, 1))
        for row in range(24):
            for column in range(34):
                a = row * 35 + column
                field_mesh.triangle(a, a + 35, a + 36)
                field_mesh.triangle(a, a + 36, a + 1)
        field = field_mesh.write(root, "KitchenField_%d" % index)
        node("Cropped field", parent=buildings,
             components=_render(field, [materials["FieldEarth"]]), static=True)

    # The road gate is far enough ahead to become a destination, not an obstacle
    # immediately in front of the player's first view.
    gate_z = 309.0
    gate_x = road_center(gate_z)
    gate_y = terrain_height(gate_x, gate_z)
    gate_reference = _gate().write(root, "RoadGate")
    gate = node("Woldmere north gate", parent=landmarks, position=[gate_x, gate_y, gate_z],
        components=_render(gate_reference, [stone]) + [_box_collider((-6.5, 4.3, 0), (3, 8.6, 2.6)),
         _box_collider((6.5, 4.3, 0), (3, 8.6, 2.6)), _box_collider((0, 8.6, 0), (10, 2, 2.6))], static=True)
    tower_reference = _tower().write(root, "GateWatchtower")
    tower_x, tower_z = gate_x + 13.8, gate_z + 2
    node("North road watchtower", parent=landmarks,
         position=[tower_x, terrain_height(tower_x, tower_z) - .3, tower_z],
         components=_render(tower_reference, [stone, timber]) +
         [_box_collider((0, 5.4, 0), (6.6, 10.8, 6.6))], static=True)

    trees = []
    for mesh_factory, count, species, foliage in ((_conifer, 3, "ScotsPine", "PineGreen"),
                                                   (_broadleaf, 2, "FieldOak", "LeafGreen")):
        for index in range(count):
            close = mesh_factory(index).write(root, "%s_%d" % (species, index))
            distant = mesh_factory(index, False).write(root, "%sDistant_%d" % (species, index))
            trees.append((close, distant, materials[foliage]))
    rocks = []
    for i in range(3):
        mesh = _Mesh()
        mesh.ellipsoid((0, .6, 0), (1.8, 1.3, 1.5), seed=91 + i, roughness=.23, rings=5, segments=9)
        rocks.append(mesh.write(root, "WeatheredBoulder_%d" % i))
    rng = random.Random(921847)
    # Broadleaf stands frame the open road. Unevenly spaced conifer belts fill the distant hills.
    clusters = [(-19, 24, 13, 7, .85), (21, 32, 14, 8, .84), (-36, 70, 20, 12, .75),
                (-50, 117, 24, 14, .57), (-40, 185, 24, 12, .7), (-68, 267, 27, 16, .42),
                (80, 279, 35, 16, .33), (-95, 390, 40, 17, .26), (102, 405, 38, 19, .24),
                (89, 527, 35, 16, .20), (-98, 551, 48, 19, .22), (154, 660, 52, 16, .18),
                (-159, 700, 55, 16, .20)]
    planted = []

    def plant(x, z, oak_chance):
        offset = abs(x - road_center(z))
        if offset < 10 or (abs(x - village_x) < 50 and abs(z - HAMLET_Z) < 48):
            return False
        if abs(x - gate_x) < 24 and abs(z - gate_z) < 27:
            return False
        if (x - (road_center(61) + 20)) ** 2 + (z - 61) ** 2 < 11 ** 2:
            return False
        if any((x - px) ** 2 + (z - pz) ** 2 < 4.8 ** 2 for px, pz in planted):
            return False
        planted.append((x, z))
        variant = rng.randrange(3, 5) if rng.random() < oak_chance else rng.randrange(3)
        close, distant, leaves = trees[variant]
        reference = close if offset < 34 and -10 < z < 104 else distant
        scale = rng.uniform(.72, 1.31) * (1.2 if offset > 110 else 1)
        node("Scots pine" if variant < 3 else "Field oak", parent=woodland,
             position=[x, _ground_height(x, z) - .10, z], scale=[scale] * 3,
             rotation=_yaw(rng.uniform(0, math.tau)), components=_render(reference, [timber, leaves]) +
             [{"Type": "GameSproutRuntime.CapsuleCollider", "Settings": {"Center": [0, 2.3, 0]},
               "Shape": {"Radius": .38, "Height": 4.6, "Axis": "Y"}}], static=True)
        return True

    for ox, cz, radius, count, oaks in clusters:
        cx = road_center(cz) + ox
        count_left = count
        for _ in range(count * 8):
            angle, distance = rng.uniform(0, math.tau), radius * math.sqrt(rng.random())
            x, z = cx + distance * math.cos(angle), cz + distance * math.sin(angle)
            if plant(x, z, oaks):
                count_left -= 1
                if count_left == 0:
                    break
    for index in range(53):
        z = rng.uniform(-65, 740)
        x = road_center(z) + (-1 if index % 2 else 1) * rng.uniform(15, 170)
        if abs(x - village_x) < 73 and abs(z - HAMLET_Z) < 89:
            continue
        scale = rng.uniform(.65, 2.0)
        node("Limestone outcrop", parent=woodland, position=[x, terrain_height(x, z) - .4, z],
             scale=[scale, scale, scale], rotation=_yaw(rng.uniform(0, math.tau)),
             components=_render(rocks[index % 3], [materials["RockSurface"]]) +
             [_box_collider((0, .6, 0), (2.8, 1.7, 2.4))], static=True)

    # Sparse route markers establish scale and gently reveal the winding road.
    marker_mesh = _Mesh()
    marker_mesh.box((0, .65, 0), (.23, 1.3, .23), 0)
    marker_mesh.box((0, 1.15, 0), (.65, .23, .15), 0)
    marker = marker_mesh.write(root, "RoadMarker")
    for z in range(30, 750, 38):
        x = road_center(z) - 6.5
        node("Waymarker", parent=landmarks, position=[x, terrain_height(x, z), z],
             rotation=_yaw(-.2), components=_render(marker, [timber]), static=True)

    verge = node("Meadow verges", parent=environment, static=True)["ID"]
    _ground_cover(root, node, verge, materials)
    _foreground_setting(root, node, buildings, materials, house_shapes)

    return {"EnvironmentID": environment, "GateID": gate["ID"],
            "HamletCenter": [village_x, terrain_height(village_x, HAMLET_Z), HAMLET_Z],
            "RoadEnd": [road_center(760), terrain_height(road_center(760), 760), 760],
            "Bounds": [TERRAIN_X_MIN, TERRAIN_Z_MIN, TERRAIN_X_MAX, TERRAIN_Z_MAX],
            "OriginHeight": terrain_height(0, 0)}


def _validate():
    """Fast isolated source-format checks; does not touch the destination project."""
    import tempfile
    with tempfile.TemporaryDirectory(prefix="woldmere-landscape-") as directory:
        objects = []
        def node(name, parent=0, position=None, scale=None, components=(), rotation=None, static=False):
            item = {"ID": len(objects) + 1, "Name": name, "ParentID": parent,
                    "Position": position, "Scale": scale, "Rotation": rotation,
                    "Components": list(components), "Static": static}
            objects.append(item)
            return item
        root = Path(directory)
        result = build_landscape(root, node)
        assert result["OriginHeight"] == 0
        for z in range(-100, 781, 3):
            assert -5 <= terrain_height(road_center(z), z) <= 8
        mesh_count = triangles = 0
        for path in (root / "Assets/Environment/Meshes").glob("*.mesh"):
            data = path.read_bytes()
            assert data[:8] == b"GSMESH02"
            version, flags, vertices, indices, sections, names = struct.unpack_from("<6I", data, 8)
            assert version == 2 and flags == 13 and indices % 3 == 0
            start = (32 + names + 3) & ~3
            expected = start + vertices * 48 + indices * 4 + sections * 12
            assert len(data) == expected, (path, len(data), expected)
            values = struct.unpack_from("<" + "f" * (vertices * 3), data, start)
            assert all(math.isfinite(value) for value in values)
            normals = struct.unpack_from("<" + "f" * (vertices * 3), data, start + vertices * 12)
            index_offset = start + vertices * 48
            mesh_indices = struct.unpack_from("<" + "I" * indices, data, index_offset)
            assert all(index < vertices for index in mesh_indices)
            for at in range(0, indices, 3):
                a, b, c = mesh_indices[at:at + 3]
                p, q, r = (values[index * 3:index * 3 + 3] for index in (a, b, c))
                area = _cross(_subtract(q, p), _subtract(r, p))
                assert sum(value * value for value in area) > 1e-14, (path, "degenerate triangle", at)
                normal = [sum(normals[index * 3 + axis] for index in (a, b, c)) for axis in range(3)]
                assert sum(x * y for x, y in zip(area, normal)) > 0, (path, "reversed winding", at)
            mesh_count += 1
            triangles += indices // 3
        all_metas = list((root / "Assets").rglob("*.meta"))
        identities = [json.loads(path.read_text())["Guid"] for path in all_metas]
        assert len(identities) == len(set(identities))
        assert all(item["Static"] for item in objects)
        known_guids = set(identities)
        for item in objects:
            for component in item["Components"]:
                if component["Type"] == "GameSproutRuntime.Mesh":
                    assert component["Mesh"]["Guid"] in known_guids
                if component["Type"] == "GameSproutRuntime.MeshRenderer":
                    assert all(reference["Guid"] in known_guids for reference in component["Materials"])
        before = {path.relative_to(root): hashlib.sha256(path.read_bytes()).digest()
                  for path in (root / "Assets").rglob("*") if path.is_file()}
        objects.clear()
        assert build_landscape(root, node) == result
        after = {path.relative_to(root): hashlib.sha256(path.read_bytes()).digest()
                 for path in (root / "Assets").rglob("*") if path.is_file()}
        assert before == after, "Procedural asset contents or identities are not deterministic"
        print("PASS landscape source validation: %d scene nodes, %d meshes, %d triangles, %d assets" %
              (len(objects), mesh_count, triangles, len(all_metas)))
        print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    _validate()
