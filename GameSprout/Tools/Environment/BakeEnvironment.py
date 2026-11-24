#!/usr/bin/env python3
"""Bake a linear HDR panorama or GameSprout cube capture into .environment."""
import argparse
import json
from pathlib import Path
import struct
import subprocess

import numpy as np


def read_panorama(source):
    if source.suffix.lower() == ".exr":
        import OpenEXR

        image = OpenEXR.File(str(source))
        channels = image.channels()
        color = channels.get("RGBA") or channels.get("RGB")
        if color is None:
            raise ValueError("OpenEXR panorama requires RGB channels")
        rgb = color.pixels[..., :3].astype(np.float32)
        height, width = rgb.shape[:2]
    else:
        info = subprocess.check_output([
            "ffprobe", "-v", "error", "-select_streams", "v:0",
            "-show_entries", "stream=width,height", "-of", "json", source,
        ])
        streams = json.loads(info)["streams"]
        if not streams:
            raise ValueError("Panorama does not contain an image")
        width, height = streams[0]["width"], streams[0]["height"]
        raw = subprocess.check_output([
            "ffmpeg", "-v", "error", "-xerror", "-i", source, "-frames:v", "1",
            "-f", "rawvideo", "-pix_fmt", "gbrpf32le", "-",
        ])
        gbr = np.frombuffer(raw, "<f4").reshape(3, height, width)
        rgb = gbr[[2, 0, 1]].transpose(1, 2, 0)
    if width != 2 * height:
        raise ValueError("Expected a 2:1 equirectangular panorama")
    if not np.isfinite(rgb).all():
        raise ValueError("Panorama contains NaN or infinity")
    return np.clip(rgb, 0, 65504, out=rgb)


def directions(face, size):
    edge = np.linspace(-1, 1, 2 * size + 1, dtype=np.float32)[1::2]
    s, t = np.meshgrid(edge, edge)
    one = np.ones_like(s)
    axes = ((one, -t, -s), (-one, -t, s), (s, one, t),
            (s, -one, -t), (s, -t, one), (-s, -t, -one))
    vectors = np.stack(axes[face], axis=-1).reshape(-1, 3)
    return vectors / np.linalg.norm(vectors, axis=1, keepdims=True)


def sample(image, direction):
    height, width = image.shape[:2]
    x, y, z = np.moveaxis(direction, -1, 0)
    # Same panorama convention for the sky, the SH projection and all mips.
    u = (0.5 - np.arctan2(z, x) / (2 * np.pi)) * width - 0.5
    v = np.clip(np.arccos(np.clip(y, -1, 1)) / np.pi * height - 0.5, 0, height - 1)
    x0, y0 = np.floor(u).astype(np.int32), np.floor(v).astype(np.int32)
    fx, fy = (u - x0)[..., None], (v - y0)[..., None]
    a = x0 % width
    b = (x0 + 1) % width
    bottom = np.minimum(y0 + 1, height - 1)
    top_rgb = image[y0, a] * (1 - fx) + image[y0, b] * fx
    bottom_rgb = image[bottom, a] * (1 - fx) + image[bottom, b] * fx
    return top_rgb * (1 - fy) + bottom_rgb * fy


def read_cube(source):
    with source.open("rb") as handle:
        magic, size, reserved = struct.unpack("<8sII", handle.read(16))
        if magic != b"GSCUBE01" or not 1 <= size <= 4096 or reserved:
            raise ValueError("Invalid GameSprout cube capture header")
        data = np.fromfile(handle, "<f2")
    if data.size != 6 * size * size * 4:
        raise ValueError("Invalid GameSprout cube capture length")
    rgb = data.reshape(6, size, size, 4)[..., :3].astype(np.float32)
    if not np.isfinite(rgb).all():
        raise ValueError("Capture contains NaN or infinity")
    return np.maximum(rgb, 0, out=rgb)


def sample_cube(cube, direction):
    size = cube.shape[1]
    x, y, z = np.moveaxis(direction, -1, 0)
    absolute = np.abs(direction)
    major = absolute.argmax(axis=-1)
    positive = np.take_along_axis(direction, major[..., None], axis=-1)[..., 0] >= 0
    face = major * 2 + (~positive)
    denominator = absolute.max(axis=-1)
    s = np.choose(face, [-z, z, x, x, x, -x]) / denominator
    t = np.choose(face, [-y, -y, z, -z, -y, -y]) / denominator
    u = np.clip((s + 1) * size * .5 - .5, 0, size - 1)
    v = np.clip((t + 1) * size * .5 - .5, 0, size - 1)
    x0, y0 = u.astype(np.int32), v.astype(np.int32)
    x1, y1 = np.minimum(x0 + 1, size - 1), np.minimum(y0 + 1, size - 1)
    fx, fy = (u - x0)[..., None], (v - y0)[..., None]
    return ((cube[face,y0,x0] * (1-fx) + cube[face,y0,x1] * fx) * (1-fy)
            + (cube[face,y1,x0] * (1-fx) + cube[face,y1,x1] * fx) * fy)


def sh_basis(x, y, z):
    return np.stack((np.ones_like(x), y, z, x, x*y, y*z, 3*z*z-1, x*z, x*x-y*y))


def convolve_diffuse(coefficients):
    norm = np.array([0.282094792, 0.488602512, 0.488602512, 0.488602512,
                     1.092548431, 1.092548431, 0.315391565, 1.092548431, 0.546274215])
    bands = np.array([1, 2/3, 2/3, 2/3, 1/4, 1/4, 1/4, 1/4, 1/4])
    return coefficients * (norm**2 * bands)[:, None]


def diffuse_cube(cube):
    size = cube.shape[1]
    s, t = np.meshgrid(np.linspace(-1, 1, size+1), np.linspace(-1, 1, size+1))
    area = np.arctan2(s*t, np.sqrt(1+s*s+t*t))
    weights = np.diff(np.diff(area, axis=0), axis=1).reshape(-1)
    coefficients = np.zeros((9, 3))
    for face in range(6):
        x, y, z = directions(face, size).T
        coefficients += np.einsum("ij,jc,j->ic", sh_basis(x,y,z), cube[face].reshape(-1,3), weights)
    return convolve_diffuse(coefficients)


def diffuse_sh(image):
    height, width = image.shape[:2]
    phi = (0.5 - (np.arange(width) + 0.5) / width) * (2 * np.pi)
    theta = ((np.arange(height) + 0.5) / height) * np.pi
    latitude_edges = np.arange(height + 1) * np.pi / height
    weights = -np.diff(np.cos(latitude_edges)) * (2 * np.pi / width)
    coefficients = np.zeros((9, 3))
    for row in range(0, height, 32):
        span = slice(row, row + 32)
        radius = np.sin(theta[span, None])
        x, z = radius * np.cos(phi), radius * np.sin(phi)
        y = np.broadcast_to(np.cos(theta[span, None]), x.shape)
        basis = sh_basis(x,y,z)
        coefficients += np.einsum("ijk,jkc,j->ic", basis, image[span], weights[span])
    return convolve_diffuse(coefficients)


def ggx_samples(count, roughness):
    bits = count.bit_length() - 1
    sequence = np.arange(count, dtype=np.uint32)
    reversed_bits = np.zeros_like(sequence)
    for bit in range(bits):
        reversed_bits |= ((sequence >> bit) & 1) << (bits - bit - 1)
    azimuth = sequence * (2 * np.pi / count)
    distribution = reversed_bits.astype(np.float32) / count
    alpha = roughness**2
    cos_theta = np.sqrt((1 - distribution) / (1 + (alpha**2 - 1) * distribution))
    sin_theta = np.sqrt(1 - cos_theta**2)
    # V=N, reflect the normal about the sampled half vector.
    z = 2 * cos_theta**2 - 1
    use = z > 0
    xy = 2 * cos_theta * sin_theta
    rays = np.stack((np.cos(azimuth) * xy, np.sin(azimuth) * xy, z), axis=-1)[use]
    return rays.astype(np.float32), z[use]


def filter_face(sample_source, size, face, samples):
    normals = directions(face, size)
    if samples is None:
        return sample_source(normals)
    rays, weights = samples
    rgb = np.empty_like(normals)
    for offset in range(0, len(normals), 512):
        n = normals[offset:offset + 512]
        up = np.where(np.abs(n[:, 1:2]) > .99, [[1, 0, 0]], [[0, 1, 0]]).astype(np.float32)
        u = np.cross(up, n)
        u /= np.linalg.norm(u, axis=1, keepdims=True)
        v = np.cross(n, u)
        rotated = (u[:, None] * rays[None, :, 0:1] + v[:, None] * rays[None, :, 1:2]
                   + n[:, None] * rays[None, :, 2:3])
        rgb[offset:offset + len(n)] = np.einsum("ijk,j->ik", sample_source(rotated), weights) / weights.sum()
    return rgb


def write_cube(output, sample_source, size, samples=None):
    for face in range(6):
        rgb = filter_face(sample_source, size, face, samples)
        rgba = np.ones((len(rgb), 4), dtype="<f2")
        rgba[:, :3] = rgb
        output.write(rgba.tobytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--sky-size", type=int, default=512, help="visible sky face size")
    parser.add_argument("--reflection-size", type=int, default=128, help="reflection face size")
    parser.add_argument("--samples", type=int, default=256, help="GGX samples per rough reflection texel")
    parser.add_argument("--lighting-clamp", type=float, help="optional radiance limit for SH and reflections, leaving the visible sky unchanged")
    args = parser.parse_args()
    if args.lighting_clamp is not None and (not np.isfinite(args.lighting_clamp) or args.lighting_clamp <= 0):
        parser.error("lighting clamp must be positive and finite")
    for size in (args.sky_size, args.reflection_size, args.samples):
        if size < 2 or size & (size - 1):
            parser.error("sizes and samples must be powers of two greater than one")
    if args.output.suffix != ".environment" or args.source.resolve() == args.output.resolve():
        parser.error("provide a separate .environment output path")
    from_cube = args.source.suffix.lower() == ".gscube"
    read = read_cube if from_cube else read_panorama
    lookup = sample_cube if from_cube else sample
    diffuse = diffuse_cube if from_cube else diffuse_sh
    image = read(args.source)
    lighting = image if args.lighting_clamp is None else np.minimum(image, args.lighting_clamp)
    coefficients = diffuse(lighting)
    levels = args.reflection_size.bit_length()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(".environment.tmp")
    try:
        with temporary.open("wb") as output:
            output.write(struct.pack("<8s4I", b"GSENV001", args.sky_size, args.reflection_size, levels, 0))
            output.write(coefficients.astype("<f4").tobytes())
            write_cube(output, lambda direction: lookup(image, direction), args.sky_size)
            for level in range(levels):
                samples = ggx_samples(args.samples, level / (levels - 1)) if level else None
                write_cube(output, lambda direction: lookup(lighting, direction), args.reflection_size >> level, samples)
        temporary.replace(args.output)
    finally:
        temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
