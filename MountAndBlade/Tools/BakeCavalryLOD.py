#!/usr/bin/env python3
"""Bake instanced color and directional-shadow meshes from the captured Cavalry gait."""

import argparse
from pathlib import Path
import struct

import fast_simplification
import numpy as np
from scipy.spatial import cKDTree

from BuildLandscape import _Mesh


PHASES = 8
PARTS = (
    ("horse", 0, 620),
    ("saddle", 1, 132),
    ("tail", 2, 50),
    ("mane", 2, 52),
    ("knight", 3, 310),
    ("shoulders", 3, 92),
    ("mail", 4, 85),
    ("face", 5, 80),
    ("helmet", 6, 79),
    ("visorPivot", 7, 20),
    ("visor", 7, 62),
)
SHADOW_PHASES = range(0, PHASES, 2)
SHADOW_PARTS = (
    ("horse", 184),
    ("tail", 18),
    ("knight", 90),
    ("helmet", 30),
)


def read_capture(directory, phase, name):
    data = (directory / f"{phase}_{name}.raw").read_bytes()
    count, index_count = struct.unpack_from("<2I", data)
    if len(data) != 8 + count * 32 + index_count * 4 or index_count % 3:
        raise ValueError(f"Malformed capture for {name}, phase {phase}")
    vertices = np.frombuffer(data, "<f4", count=count * 8, offset=8).reshape((-1, 8))
    triangles = np.frombuffer(data, "<u4", count=index_count, offset=8 + count * 32).reshape((-1, 3))
    if not np.isfinite(vertices).all() or triangles.max() >= count:
        raise ValueError(f"Invalid source vertices for {name}, phase {phase}")
    return vertices, triangles


def surface_binding(points, positions, faces):
    """Map each reduced vertex onto a nearby source triangle for pose and UV transfer."""
    corners = positions[faces].astype(np.float64)
    centres = corners.mean(axis=1)
    _, candidates = cKDTree(centres).query(points, k=min(len(faces), 24))
    candidates = candidates.reshape((len(points), -1))
    triangles = corners[candidates]
    a = triangles[:, :, 0]
    ab = triangles[:, :, 1] - a
    ac = triangles[:, :, 2] - a
    ap = points[:, None, :] - a
    dot = lambda left, right: np.sum(left * right, axis=-1)
    ab2, ac2, abac = dot(ab, ab), dot(ac, ac), dot(ab, ac)
    apab, apac = dot(ap, ab), dot(ap, ac)
    det = np.maximum(ab2 * ac2 - abac**2, 1e-20)
    v = (ac2 * apab - abac * apac) / det
    w = (ab2 * apac - abac * apab) / det
    weights = np.maximum(np.stack((1 - v - w, v, w), axis=-1), 0)
    weights /= weights.sum(axis=-1, keepdims=True)
    projected = np.einsum("ijk,ijkl->ijl", weights, triangles)
    best = np.argmin(dot(projected - points[:, None, :], projected - points[:, None, :]), axis=1)
    indices = np.arange(len(points))
    return faces[candidates[indices, best]], weights[indices, best]


def reduce_part(capture, name, limit):
    reference, faces = read_capture(capture, 0, name)
    if len(faces) <= limit:
        return faces, [read_capture(capture, phase, name)[0] for phase in range(PHASES)], len(faces)

    # Weld positions before QEM so FBX attribute seams cannot strand small triangles.
    # UVs and normals come back from the original triangles after reduction.
    positions = reference[:, :3]
    _, unique, inverse = np.unique(np.rint(positions * 100000).astype(np.int64),
                                   axis=0, return_index=True, return_inverse=True)
    welded = inverse[faces].astype(np.int32)
    good = ((welded[:, 0] != welded[:, 1]) &
            (welded[:, 1] != welded[:, 2]) &
            (welded[:, 2] != welded[:, 0]))
    points, reduced_faces = fast_simplification.simplify(
        positions[unique].astype(np.float64), welded[good], target_count=limit, agg=10)
    if not len(reduced_faces):
        raise ValueError(f"Reduction of {name} produced {len(reduced_faces)} triangles for a limit of {limit}")

    bindings, weights = surface_binding(points, positions, faces[good])
    frames = []
    for phase in range(PHASES):
        source, source_faces = read_capture(capture, phase, name)
        if not np.array_equal(source_faces, faces):
            raise ValueError(f"Source topology of {name} changed during the Canter cycle")
        vertices = np.einsum("ij,ijk->ik", weights, source[bindings])
        magnitudes = np.linalg.norm(vertices[:, 3:6], axis=-1, keepdims=True)
        vertices[:, 3:6] /= np.maximum(magnitudes, 1e-8)
        frames.append(vertices)
    return reduced_faces, frames, len(faces)


def append_part(mesh, indices, vertices, slot):
    start = len(mesh.positions)
    mesh.positions.extend(map(tuple, vertices[:, :3]))
    mesh.normals.extend(map(tuple, vertices[:, 3:6]))
    mesh.uvs.extend(map(tuple, vertices[:, 6:8]))
    mesh.colors.extend([(1.0, 1.0, 1.0, 1.0)] * len(vertices))
    mesh.sections.setdefault(slot, []).extend((indices.flatten() + start).tolist())


def write_mesh(mesh, project, name):
    mesh.write(project, name, "Characters/CavalryLOD")
    bounds = np.array(mesh.positions)
    triangles = sum(len(section) for section in mesh.sections.values()) // 3
    print(f"{name}: {len(bounds)} vertices, {triangles} triangles, "
          f"bounds {bounds.min(axis=0).round(3)} – {bounds.max(axis=0).round(3)}")


def bake_color(capture, project):
    frames = [_Mesh() for _ in range(PHASES)]
    for name, slot, budget in PARTS:
        indices, poses, source_triangles = reduce_part(capture, name, budget)
        print(f"{name:12s} {source_triangles:6d} → {len(indices):4d} triangles", flush=True)
        for mesh, vertices in zip(frames, poses):
            append_part(mesh, indices, vertices, slot)
    for phase, mesh in enumerate(frames):
        write_mesh(mesh, project, f"Canter{phase:02d}")


def bake_shadows(capture, project):
    # Depth-only batches reuse a single section for the horse and rider.
    # The moving legs, tail and helmet still follow the source gait.
    frames = [_Mesh() for _ in SHADOW_PHASES]
    for name, budget in SHADOW_PARTS:
        indices, poses, _ = reduce_part(capture, name, budget)
        for mesh, phase in zip(frames, SHADOW_PHASES):
            append_part(mesh, indices, poses[phase], 0)
    for phase, mesh in zip(SHADOW_PHASES, frames):
        write_mesh(mesh, project, f"CanterShadow{phase:02d}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, help="Directory of .raw poses from CaptureCavalryPoses.h")
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--shadows-only", action="store_true", help="Refresh the depth-only meshes")
    args = parser.parse_args()
    if not args.shadows_only:
        bake_color(args.capture, args.project)
    bake_shadows(args.capture, args.project)


if __name__ == "__main__":
    main()
