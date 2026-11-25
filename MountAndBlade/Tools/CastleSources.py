#!/usr/bin/env python3
"""Stage the daytime castle source and the portable PBR inputs it references."""

import argparse
from contextlib import contextmanager
from hashlib import sha256
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import tarfile
from tempfile import TemporaryDirectory


DAY_SCENE = 'Assets/The_Modular_Medieval_Castle/Scenes/MC_Demo_Day.unity'
TERRAIN = 'Assets/The_Modular_Medieval_Castle/Other/MC_Terrain.asset'
GUID = re.compile(r'guid:\s*([a-f0-9]{32})')
MEMBER = re.compile(r'([a-f0-9]{32})/(asset|asset\.meta|pathname)')
GEOMETRY = frozenset({'.fbx', '.mat', '.prefab', '.unity'})
HERO_TEXTURES = ('MC_Castle_Wall', 'MC_Castle_Stone_Floor', 'MC_Trim_Sheet', 'MC_Door_')
UNITY_BLOCK = re.compile(r'^--- !u!(\d+) &(-?\d+)(?: stripped)?\n(.*?)(?=^--- !u!|\Z)', re.M | re.S)


@contextmanager
def open_package(filename):
    # Unity packages can contain an extended gzip header. Use gzip's full
    # header parser and consume the stream once per pass without tar extraction.
    with subprocess.Popen(['gzip', '-dc', str(filename)], stdout=subprocess.PIPE) as inflater:
        try:
            with tarfile.open(fileobj=inflater.stdout, mode='r|', bufsize=1 << 20) as archive:
                yield archive
            while inflater.stdout.read(1 << 20):
                pass  # Read through gzip's checksum after tar's end marker.
            if inflater.wait():
                raise ValueError('Unable to decompress the Unity package')
        finally:
            if inflater.poll() is None:
                inflater.terminate()


def catalogue(filename):
    assets = {}
    with open_package(filename) as archive:
        for entry in archive:
            match = MEMBER.fullmatch(entry.name)
            if match is None or not entry.isfile():
                continue
            guid, kind = match.groups()
            asset = assets.setdefault(guid, {})
            source = archive.extractfile(entry)
            if kind == 'pathname' and entry.size <= 4096:
                path = PurePosixPath(source.read().decode('utf-8').splitlines()[0])
                if not path.parts or path.parts[0] != 'Assets' or '..' in path.parts:
                    raise ValueError(f'Unexpected Unity source path {path}')
                asset['Path'] = path.as_posix()
            elif kind == 'asset.meta' and entry.size <= 512 * 1024:
                asset['Meta'] = source.read()
            elif kind == 'asset' and entry.size <= 8 * 1024 * 1024:
                prefix = source.read(12)
                if prefix.startswith(b'%YAML') or prefix.startswith(b'\xef\xbb\xbf%YAML'):
                    asset['YAML'] = (prefix + source.read()).decode('utf-8-sig')
    return {guid: asset for guid, asset in assets.items() if 'Path' in asset}


def castle_prefabs(scene):
    blocks = UNITY_BLOCK.findall(scene)
    castle = next(file_id for kind, file_id, body in blocks
                  if kind == '1' and re.search(r'^  m_Name: Castle$', body, re.M))
    parent = next(file_id for kind, file_id, body in blocks
                  if kind == '4' and re.search(rf'^  m_GameObject: \{{fileID: {castle}\}}$', body, re.M))
    match_parent = re.compile(rf'^    m_TransformParent: \{{fileID: {parent}\}}$', re.M)
    # Include per-instance material references along with the source prefab.
    return {guid for kind, _, body in blocks if kind == '1001' and match_parent.search(body)
            for guid in GUID.findall(body) if not guid.startswith('00000000')}


def referenced_assets(assets, seeds):
    seen = set(seeds)
    pending = list(seeds)
    for guid in pending:
        asset = assets[guid]
        links = asset.get('YAML', '') + asset.get('Meta', b'').decode('utf-8')
        for child in GUID.findall(links):
            if child not in assets or child in seen:
                continue
            seen.add(child)
            pending.append(child)
    return seen


def number(text, key, fallback):
    match = re.search(rf'^\s*(?:- )?{re.escape(key)}: (\S+)\s*$', text, re.M)
    return float(match[1]) if match else fallback


def components(text, key, labels, fallback):
    match = re.search(rf'^\s*(?:- )?{re.escape(key)}: \{{([^}}]*)\}}', text, re.M)
    if not match:
        return list(fallback)
    fields = dict(part.strip().split(': ', 1) for part in match[1].split(','))
    return [float(fields.get(label, default)) for label, default in zip(labels, fallback)]


def clamp(value):
    return min(1.0, max(0.0, value))


def linear_color(parts):
    return [value / 12.92 if value <= .04045 else ((value + .055) / 1.055) ** 2.4
            for value in parts[:3]] + [parts[3]]


def map_materials(assets, reachable):
    materials = {}
    textures = {}

    def texture(text, names, normal=False):
        for name in names:
            slot = re.search(rf'^    - {re.escape(name)}:\s*\n((?:        .*\n)+)', text, re.M)
            linked = GUID.search(slot[1]) if slot else None
            if linked is None or linked[1] not in assets:
                continue
            guid = linked[1]
            source = assets[guid]
            path = PurePosixPath(source['Path'])
            if path.suffix.lower() != '.tga':
                continue  # Unity's EXR sky and baked lighting use separate paths.
            meta = source.get('Meta', b'').decode('utf-8')
            png = path.with_suffix('.png').as_posix()
            textures[guid] = {'SourcePath': path.as_posix(), 'Path': png,
                              'ColorSpace': 'Linear' if normal or number(meta, 'sRGBTexture', 1) == 0 else 'SRGB',
                              'NormalMap': normal, 'FlipGreen': normal and number(meta, 'flipGreenChannel', 0) != 0}
            scale = components(slot[1], 'm_Scale', 'xy', (1, 1))
            offset = components(slot[1], 'm_Offset', 'xy', (0, 0))
            return png, scale + offset
        return None, [1, 1, 0, 0]

    for guid in sorted(reachable):
        source = assets[guid]
        path = PurePosixPath(source['Path'])
        if path.suffix != '.mat':
            continue
        text = source.get('YAML', '')
        base, base_st = texture(text, ('_BaseMap', '_Base_Color', '_MainTex'))
        normal, normal_st = texture(text, ('_BumpMap', '_NormalMap', '_Normal'), normal=True)
        if '_Tiling_Main_Texture:' in text:
            base_st = components(text, '_Tiling_Main_Texture', 'rgba', base_st)
            normal_st = base_st
        color = components(text, '_Color', 'rgba', (1, 1, 1, 1))
        color = components(text, '_BaseColor', 'rgba', color)
        emission = components(text, '_EmissionColor', 'rgba', (0, 0, 0, 1))
        mode = number(text, '_Mode', 0)
        alpha_clip = mode == 1 or number(text, '_AlphaClip', 0) > 0 or 'RenderType: TransparentCutout' in text
        alpha_blend = mode in (2, 3) or number(text, '_Surface', 0) > 0 or 'RenderType: Transparent\n' in text
        smoothness = number(text, '_Smoothness', number(text, '_Glossiness', .45))
        materials[guid] = {
            'Path': path.as_posix(), 'Name': path.stem,
            'BaseColorTexture': base, 'BaseColorTextureST': base_st,
            'NormalTexture': normal, 'NormalTextureST': normal_st,
            'BaseColor': [clamp(value) for value in linear_color(color)],
            'Metallic': clamp(number(text, '_Metallic', 0)), 'Roughness': 1 - clamp(smoothness),
            'NormalScale': max(0.0, number(text, '_BumpScale', 1)),
            'AlphaMode': 'Cutout' if alpha_clip else 'Blend' if alpha_blend else 'Opaque',
            'AlphaCutoff': clamp(number(text, '_Cutoff', .5)),
            'DoubleSided': number(text, '_Cull', 2) == 0,
            'Emission': linear_color(emission) if '_EMISSION' in text else [0, 0, 0, 1],
        }
    return materials, textures


def digest(path):
    with path.open('rb') as stream:
        return sha256(stream.read()).hexdigest()


def decode_terrain(path):
    """Read the validated TerrainData layout used by this package version."""
    data = path.read_bytes()
    if sha256(data).hexdigest() != 'ac0cf658fae3ef36ba277fe052e75bab981ab615a87b70972aff98ed25354a47':
        raise ValueError('The castle TerrainData has changed; verify its heightfield layout before converting')
    source_resolution, stride = 2049, 8
    count_offset = 1898220
    if struct.unpack_from('<I', data, count_offset)[0] != source_resolution ** 2:
        raise ValueError('The castle TerrainData height sample count is invalid')
    cell_x, height_scale, cell_z = struct.unpack_from('<3f', data, 10557196)
    sample_start = count_offset + 4
    samples = [struct.unpack_from('<H', data, sample_start + 2 * (z * source_resolution + x))[0]
               for z in range(0, source_resolution, stride) for x in range(0, source_resolution, stride)]
    return {'Origin': [-130, -20, -142], 'Step': [stride * cell_x, stride * cell_z],
            'ScaleY': height_scale / 32766, 'Resolution': (source_resolution - 1) // stride + 1,
            'Samples': samples}


def convert_texture(stream, output, scratch, texture):
    with scratch.open('wb') as raw:
        shutil.copyfileobj(stream, raw)
    # Unity UV channels and ST are retained by the layout converter. Match the
    # bridge's vertical pixel transform for direct TGA imports as well.
    limit = 2048 if output.name.startswith(HERO_TEXTURES) else 1024
    with scratch.open('rb') as raw:
        width, height = struct.unpack_from('<HH', raw.read(18), 12)
    if texture['FlipGreen']:
        # Only used when the Unity texture's source importer requests it.
        ratio = min(1.0, limit / max(width, height))
        filters = f'format=rgba,vflip,lutrgb=g=negval,scale={max(1, round(width*ratio))}:{max(1, round(height*ratio))}'
        command = ['ffmpeg', '-v', 'error', '-y', '-i', str(scratch), '-frames:v', '1', '-vf', filters, str(output)]
    else:
        command = ['/usr/bin/sips', '-s', 'format', 'png', '-f', 'vertical']
        if max(width, height) > limit:
            command += ['-Z', str(limit)]
        command += [str(scratch), '--out', str(output)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode or not output.is_file():
        raise ValueError(f'Texture conversion failed for {texture["SourcePath"]}: {result.stderr.strip()}')
    texture['SourceSHA256'] = digest(scratch)
    texture['SHA256'] = digest(output)
    texture['MaxSize'] = limit


def extract(archive_path, target, max_bytes):
    assets = catalogue(archive_path)
    scene = next(guid for guid, asset in assets.items() if asset['Path'] == DAY_SCENE)
    terrain = next(guid for guid, asset in assets.items() if asset['Path'] == TERRAIN)
    seeds = castle_prefabs(assets[scene]['YAML'])
    if not seeds:
        raise ValueError('The daytime scene has no Castle prefab instances')
    reachable = referenced_assets(assets, seeds) | {scene, terrain}
    materials, textures = map_materials(assets, reachable)
    paths = {guid: asset['Path'] for guid, asset in assets.items()
             if guid in reachable and (PurePosixPath(asset['Path']).suffix in GEOMETRY or asset['Path'] == TERRAIN)}
    paths.update({guid: texture['Path'] for guid, texture in textures.items()})
    remaining = set(paths)
    total = 0
    target.mkdir(parents=True, exist_ok=False)
    with TemporaryDirectory(prefix='castle-tga-', dir=target) as directory, open_package(archive_path) as archive:
        scratch = Path(directory) / 'source.tga'
        for entry in archive:
            match = MEMBER.fullmatch(entry.name)
            if not entry.isfile() or match is None or match[2] != 'asset' or match[1] not in remaining:
                continue
            guid = match[1]
            output = target / paths[guid]
            output.parent.mkdir(parents=True, exist_ok=True)
            source = archive.extractfile(entry)
            if guid in textures:
                convert_texture(source, output, scratch, textures[guid])
            else:
                with output.open('wb') as data:
                    shutil.copyfileobj(source, data)
            meta = assets[guid].get('Meta', b'')
            if not meta or GUID.search(meta.decode('utf-8')) is None:
                raise ValueError(f'No Unity identity for {paths[guid]}')
            output.with_name(output.name + '.meta').write_bytes(meta)
            total += output.stat().st_size + len(meta)
            if total > max_bytes:
                raise ValueError(f'Staged castle exceeds {max_bytes // 2**20} MiB')
            remaining.remove(guid)
            if (len(paths) - len(remaining)) % 100 == 0:
                print(f'{len(paths) - len(remaining)}/{len(paths)} assets · {total / 2**20:.1f} MiB', flush=True)
    if remaining:
        raise ValueError(f'Missing staged castle assets: {[paths[guid] for guid in sorted(remaining)]}')
    for filename, values in (('Materials.json', materials), ('Textures.json', textures),
                             ('TerrainHeight.json', decode_terrain(target / TERRAIN))):
        (target / filename).write_text(json.dumps(values, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(f'{len(paths)} assets · {len(materials)} materials · {len(textures)} textures · {total / 2**20:.1f} MiB', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('output', type=Path, help='A fresh temporary source directory')
    parser.add_argument('--max-mib', type=int, default=500)
    args = parser.parse_args()
    if args.max_mib <= 0:
        parser.error('--max-mib must be positive')
    extract(args.archive, args.output, args.max_mib * 2**20)
