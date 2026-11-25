"""Procedural, project-local cloud cover for the valley's perspective camera."""

import math
from pathlib import Path
import random
import struct
import zlib

from BuildLandscape import _Mesh, _meta, _reference, _render, _smooth, _write_json


_SHADER = '''/* GameSproutShader {
    "Name": "Woldmere/Valley Clouds",
    "Passes": [{ "Name": "Forward", "LightMode": "Forward", "Vertex": "VSMain", "Fragment": "PSMain",
        "Cull": "Off", "Blend": "Alpha", "DepthTest": true, "DepthWrite": false, "DepthCompare": "LessEqual" }]
} */
/* GameSproutMaterial {
    "Name": "Woldmere/Valley Clouds",
    "Properties": [{ "Name": "CloudTexture", "DisplayName": "云层贴图", "Type": "Texture", "Default": null }]
} */

cbuffer GameSproutDraw : register(b0, space1) {
    column_major float4x4 Model;
    column_major float4x4 ViewProjection;
    column_major float4x4 NormalMatrix;
};

struct SkyVertex {
    [[vk::location(0)]] float3 Direction : TEXCOORD0;
    [[vk::location(3)]] float2 UV : TEXCOORD3;
};
struct SkySample {
    float4 Position : SV_Position;
    float2 UV : TEXCOORD0;
};

SkySample VSMain(SkyVertex input) {
    SkySample output;
    // A direction (w = 0) ignores model and camera translation. Place cloud
    // fragments at the far plane so terrain and characters occlude them.
    float4 direction = mul(Model, float4(input.Direction, 0));
    output.Position = mul(ViewProjection, direction);
    output.Position.z = output.Position.w;
    output.UV = input.UV;
    return output;
}

Texture2D<float4> CloudTexture : register(t0, space2);
SamplerState CloudSampler : register(s0, space2);

float4 PSMain(SkySample input) : SV_Target0 {
    return CloudTexture.Sample(CloudSampler, input.UV);
}
'''


def _periodic_noise(columns, rows, rng):
    values = [[rng.random() for _ in range(columns)] for _ in range(rows + 1)]

    def sample(u, v):
        x, y = u * columns, v * rows
        i, j = int(x), int(y)
        a, b = x - i, y - j
        a, b = a * a * (3 - 2 * a), b * b * (3 - 2 * b)
        left, right = i % columns, (i + 1) % columns
        upper, lower = values[j], values[j + 1]
        top = upper[left] + (upper[right] - upper[left]) * a
        bottom = lower[left] + (lower[right] - lower[left]) * a
        return top + (bottom - top) * b

    return sample


def _source_cloud_pixels(width, height):
    rng = random.Random(281131)
    size = width * height
    density = [0.0] * size
    underbelly = [0.0] * size
    longitude = [(x + .5) / width for x in range(width)]
    altitude = [(y + .5) / height for y in range(height)]
    # Separate weather systems leave generous clear sky between clouds, with
    # thin trails lower on the horizon and more rounded cumulus farther up.
    groups = ((.965, .20, .057, .092, 12), (.055, .163, .040, .065, 9),
              (.168, .36, .052, .146, 14), (.294, .248, .080, .083, 14),
              (.402, .148, .055, .043, 10), (.521, .367, .096, .125, 17),
              (.668, .243, .067, .095, 13), (.787, .320, .054, .139, 12),
              (.878, .137, .040, .045, 8), (.063, .623, .059, .097, 11),
              (.449, .712, .094, .085, 13), (.716, .682, .057, .102, 11))
    for cx, cy, width_u, height_v, puffs in groups:
        for index in range(puffs):
            across = 2 * index / (puffs - 1) - 1
            u = (cx + width_u * across * .87 + width_u * rng.uniform(-.10, .10)) % 1
            v = cy + height_v * (.75 * (1 - across * across) + rng.uniform(-.28, .24) - .32)
            rx = width_u * rng.uniform(.22, .49)
            ry = height_v * rng.uniform(.38, .82)
            for row in range(max(0, int((v - ry) * height)), min(height, math.ceil((v + ry) * height))):
                py = (altitude[row] - v) / ry
                for column in range(int((u - rx) * width), math.ceil((u + rx) * width)):
                    x = column % width
                    px = ((longitude[x] - u + .5) % 1 - .5) / rx
                    distance = px * px + py * py
                    if distance >= 1:
                        continue
                    weight = (1 - distance) ** .72
                    offset = row * width + x
                    if weight > density[offset]:
                        density[offset] = weight
                        underbelly[offset] = (altitude[row] - cy) / height_v - .18 * px

    broad = _periodic_noise(61, 22, rng)
    curls = _periodic_noise(127, 49, rng)
    grain = _periodic_noise(279, 111, rng)
    # Emit RGB in sRGB and linear alpha. The transparent mip chain is generated
    # by the asset importer with colour bleeding into transparent texels.
    pixels = bytearray(size * 4)
    for index, weight in enumerate(density):
        if weight <= .11:
            continue
        row, column = divmod(index, width)
        u, v = longitude[column], altitude[row]
        large, medium, small = broad(u, v), curls(u, v), grain(u, v)
        turbulence = .31 * (large - .5) + .17 * (medium - .5) + .065 * (small - .5)
        alpha = _smooth(.21, .77, weight + turbulence) * _smooth(.01, .095, v)
        if alpha <= 0:
            continue
        light = _smooth(-.64, 1.19, underbelly[index]) * .73 + .18 * large + .09 * medium
        shadow, sunlight = (167, 182, 203), (255, 249, 232)
        offset = index * 4
        for channel in range(3):
            pixels[offset + channel] = round(shadow[channel] + (sunlight[channel] - shadow[channel]) * light)
        pixels[offset + 3] = round(alpha * 247)
    return pixels


def _write_cloud_texture(root, source):
    width, height = 1536, 384
    pixels = _source_cloud_pixels(width, height)
    stride = width * 4

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    rows = b"".join(b"\0" + pixels[y * stride:(y + 1) * stride] for y in range(height))
    target = root / "Assets" / source
    target.parent.mkdir(parents=True, exist_ok=True)
    header = struct.pack(">2I5B", width, height, 8, 6, 0, 0, 0)
    target.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 8))
                       + chunk(b"IEND", b""))
    _meta(root, source, "Texture", 1, {"ColorSpace": "SRGB", "AlphaSource": "FromSource",
         "AlphaIsTransparency": True, "GenerateMipmaps": True, "FilterMode": "Trilinear",
         "WrapU": "Repeat", "WrapV": "ClampToEdge", "MaxSize": 2048})


def _dome():
    mesh = _Mesh()
    longitude, latitude = 64, 16
    for ring in range(latitude):
        t = ring / latitude
        elevation = math.radians(90 * t - 1.5 * (1 - t))
        radius, y = math.cos(elevation), math.sin(elevation)
        for step in range(longitude + 1):
            u = step / longitude
            angle = u * math.tau
            position = (radius * math.sin(angle), y, radius * math.cos(angle))
            # Most visible clouds occupy the lower sky. Concentrate UV space
            # there while allowing the same cap to cover the full hemisphere.
            v = t ** .59
            mesh.vertex(position, tuple(-component for component in position), (u, v))
    columns = longitude + 1
    for ring in range(latitude - 1):
        for step in range(longitude):
            a = ring * columns + step
            b = a + columns
            mesh.triangle(a, b, b + 1)
            mesh.triangle(a, b + 1, a + 1)
    for step in range(longitude):
        top = mesh.vertex((0, 1, 0), (0, -1, 0), ((step + .5) / longitude, 1.0))
        a = (latitude - 1) * columns + step
        mesh.triangle(a, top, a + 1)
    return mesh


def build_sky(root, node):
    """Write one static transparent draw above the Runtime gradient sky."""
    root = Path(root)
    texture = "Environment/Textures/ValleyClouds.png"
    shader = "Environment/Shaders/ValleyClouds.shader"
    material = "Environment/Materials/ValleyClouds.mat"
    _write_cloud_texture(root, texture)
    path = root / "Assets" / shader
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(_SHADER, encoding="utf-8")
    _meta(root, shader, "Shader", 6)
    _write_json(root / "Assets" / material, {"Version": 1, "Name": "Valley Clouds",
        "Shader": _reference(shader), "CloudTexture": _reference(texture),
        "SortingPriority": -50, "EnableGPUInstancing": False})
    _meta(root, material, "Material", 5)
    components = _render(_dome().write(root, "ValleyCloudDome"), [_reference(material)])
    components[1]["ShadowCastingMode"] = "Off"
    components[1]["ReceiveShadows"] = False
    # The authored mesh is above the playable world and selection rays. Its
    # shader projects directions only, which keeps the sky fixed as we ride.
    return node("Valley clouds", position=(0, 700, 0), components=components, static=True)
