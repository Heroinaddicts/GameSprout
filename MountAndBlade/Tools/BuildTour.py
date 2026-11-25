#!/usr/bin/env python3
"""Assemble the mounted tour from the GameSprout importer model description."""

import argparse
import copy
import json
import math
import re
import uuid
from pathlib import Path

from BuildLandscape import build_landscape, terrain_height
from BuildValleySky import build_sky

ROOT = Path(__file__).resolve().parent.parent
SCENE_PATH = 'Scenes/ValleyRoad.scene'
SCRIPT_GUID = '8245786937884d97a5ab243dd3e3ed21'
SCRIPT_CPP_GUID = '911422445857410088d5dfd04dba5e5e'
IDENTITY = (0, 0, 0, 1)
UNIT_SCALE = (1, 1, 1)


def save(path, contents):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(contents, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def write_asset(root, relative, importer, type_id, contents):
    asset = root / 'Assets' / relative
    save(asset, contents)
    guid = uuid.uuid5(uuid.NAMESPACE_URL, 'gamesprout:mount-and-blade:' + relative).hex
    save_defaults(Path(str(asset) + '.meta'), {
        'Version': 1, 'Guid': guid, 'ImporterType': importer, 'Path': relative, 'ImportSettings': {},
        'SubAssets': [{'Type': type_id, 'SourceKey': 'main', 'LocalID': 1, 'Name': asset.stem}],
    })
    return {'Guid': guid, 'LocalID': 1}


def save_defaults(path, defaults):
    current = json.loads(path.read_text(encoding='utf-8')) if path.exists() else {}
    missing = set(defaults) - set(current)
    if missing:
        for key, value in defaults.items():
            current.setdefault(key, value)
        save(path, current)


def multiply(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def inverse(rotation):
    return (-rotation[0], -rotation[1], -rotation[2], rotation[3])


def rotate(rotation, vector):
    return multiply(multiply(rotation, (*vector, 0)), inverse(rotation))[:3]


def rotation(pitch, yaw):
    x, y = math.radians(pitch) / 2, math.radians(yaw) / 2
    return multiply((0, math.sin(y), 0, math.cos(y)), (math.sin(x), 0, 0, math.cos(x)))


class Scene:
    def __init__(self):
        self.objects = []
        self.next_id = 1

    def identity(self):
        current = self.next_id
        self.next_id += 1
        return current

    def node(self, name, parent=0, position=None, scale=None, components=(), rotation=None, static=False):
        node = {'ID': self.identity(), 'ParentID': parent, 'Name': name, 'Active': True,
                'Components': [{'Type': 'GameSproutRuntime.Transform', 'Position': position or (0, 0, 0),
                                'Rotation': rotation or IDENTITY, 'Scale': scale or UNIT_SCALE}]}
        if static:
            node['StaticFlags'] = 128  # ShadowCasterStatic
        node['Components'].extend(copy.deepcopy(components))
        for component in node['Components']:
            component['ID'] = self.identity()
        self.objects.append(node)
        return node


def find_clip(clips, name):
    for clip in clips:
        if name in re.split(r'[^a-z]+', clip['Name'].lower()):
            return clip['Ref']
    raise ValueError(f'No {name} clip found in {", ".join(clip["Name"] for clip in clips)}')


def model_nodes(scene, nodes, root):
    items = {}
    poses = {}
    pending = set(range(len(nodes)))
    while pending:
        available = [i for i in sorted(pending) if nodes[i]['Parent'] not in pending]
        if not available:
            raise ValueError('Cyclic hierarchy in imported model')
        for i in available:
            source = nodes[i]
            parent = source['Parent']
            transform = source['Transform']
            if scene is not None:
                items[i] = scene.node(source['Name'] or 'Skeleton Root', items[parent]['ID'] if parent >= 0 else root['ID'],
                                      position=transform['Translation'], rotation=transform['Rotation'], scale=transform['Scale'])
            ppos, prot, pscale = poses.get(parent, ((0, 0, 0), IDENTITY, UNIT_SCALE))
            offset = rotate(prot, [s * v for s, v in zip(pscale, transform['Translation'])])
            poses[i] = (tuple(a + b for a, b in zip(ppos, offset)), multiply(prot, transform['Rotation']),
                        tuple(a * b for a, b in zip(pscale, transform['Scale'])))
            pending.remove(i)
    return items, poses


def model_meshes(scene, supplied, items, materials, rig=None):
    skinned_ids = set(supplied['SkinnedMeshes'])
    for i, source in enumerate(supplied['Model']['Nodes']):
        if 'Mesh' not in source or source['Name'] not in materials:
            continue
        mesh = source['Mesh']
        components = [
            {'Type': 'GameSproutRuntime.Mesh', 'Mesh': mesh},
            {'Type': 'GameSproutRuntime.MeshRenderer', 'Visible': True, 'Materials': materials[source['Name']]},
        ]
        if mesh['LocalID'] in skinned_ids:
            components.append({'Type': 'GameSproutRuntime.SkinnedMeshRenderer', 'AnimatorObjectFileID': rig['ID']})
        transform = source['GeometryTransform']
        scene.node(source['Name'] + ' Geometry', items[i]['ID'], position=transform['Translation'],
                   rotation=transform['Rotation'], scale=transform['Scale'], components=components)


def normalized(values):
    length = math.sqrt(sum(x * x for x in values))
    return tuple(x / length for x in values)


def direction_rotation(a, b):
    a, b = normalized(a), normalized(b)
    cross = (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
    return normalized((*cross, 1 + sum(x * y for x, y in zip(a, b))))


def texture(root, name):
    path = root / f'Assets/Characters/Knight/{name}.png.meta'
    return {'Guid': json.loads(path.read_text())['Guid'], 'LocalID': 1}


def knight_material(root, name, albedo=None, normal=None, color=(1, 1, 1), metallic=.18, roughness=.63):
    material = {'Version': 1, 'Name': name, 'Shader': 'GameSprout/Lit', 'BaseColor': [*color, 1],
                'Roughness': roughness, 'Metallic': metallic}
    if albedo:
        material['BaseColorTexture'] = texture(root, albedo)
        material['BaseColorTextureST'] = [1, -1, 0, 1]
    if normal:
        material['NormalTexture'] = texture(root, normal)
        material['NormalTextureST'] = [1, -1, 0, 1]
    return [write_asset(root, f'Characters/Knight/Materials/{name}.mat', 'Material', 5, material)]


def create_knight(scene, root, seat, supplied):
    body = supplied['Body']
    model = body['Model']
    nodes = copy.deepcopy(model['Nodes'])
    names = {entry['Name']: index for index, entry in enumerate(nodes)}
    _, rest = model_nodes(None, nodes, None)
    pelvis = rest[names['Bip001 Pelvis']][0]
    facing = rotation(0, 180)
    bind_offset = rotate(facing, tuple(-x for x in pelvis))
    knight = scene.node('Mounted Knight', seat['ID'], position=bind_offset, rotation=facing, components=(
        {'Type': 'GameSproutRuntime.Animator', 'Skeleton': model['Skeleton'], 'Speed': 1, 'PlayOnStart': False,
         'Loop': False, 'RootMotionMode': 'Disabled', 'Bindings': []},
    ))

    def aim(bone, child, direction):
        index, child_index = names['Bip001 ' + bone], names['Bip001 ' + child]
        _, poses = model_nodes(None, nodes, None)
        parent = poses[nodes[index]['Parent']][1]
        old = tuple(b - a for a, b in zip(poses[index][0], poses[child_index][0]))
        offset = direction_rotation(old, direction)
        transform = nodes[index]['Transform']
        transform['Rotation'] = multiply(multiply(inverse(parent), multiply(offset, parent)), transform['Rotation'])

    # Aim authored bone segments in the knight's FBX space, where -Z is forward.
    for side, sign in (('L', 1), ('R', -1)):
        aim(f'{side} Thigh', f'{side} Calf', (.48 * sign, -.64, -.53))
        aim(f'{side} Calf', f'{side} Foot', (.13 * sign, -.96, .22))
        aim(f'{side} Foot', f'{side} Toe0', (0, -.37, -.94))
        aim(f'{side} UpperArm', f'{side} Forearm', (.10 * sign, -.93, -.34))
        aim(f'{side} Forearm', f'{side} Hand', (-.22 * sign, -.13, -.96))
    knight['Components'][-1]['DefaultClip'] = write_asset(root, 'Characters/Knight/MountedPose.anim',
        'AnimationClip', 14, {'Version': 1, 'Duration': 0, 'SampleRate': 30,
        'Tracks': [entry['Path'] for entry in nodes], 'Parents': [entry['Parent'] for entry in nodes],
        'Frames': [{'Time': 0, 'Poses': [entry['Transform'] for entry in nodes]}]})
    items, _ = model_nodes(scene, nodes, knight)
    knight['Components'][-1]['Bindings'] = [{'Path': item['Path'], 'ObjectFileID': items[i]['ID']}
                                            for i, item in enumerate(nodes)]
    armor = knight_material(root, 'Weathered Steel', 'Body_BaseColor', 'Body_Normal', metallic=.26, roughness=.47)
    mail = knight_material(root, 'Mail', color=(.20, .19, .17), metallic=.23, roughness=.74)
    face = knight_material(root, 'Skin', color=(.22, .12, .07), metallic=0, roughness=.78)
    model_meshes(scene, body, items, {'Knight_base_LOD0': armor, 'Shoulders_LOD0': armor,
                                     'Chainmail_LOD0': mail, 'KnightFace_LOD0': face}, knight)
    helmet = knight_material(root, 'Helmet', 'Helmet_BaseColor', 'Helmet_Normal', metallic=.35, roughness=.39)
    visor = knight_material(root, 'Visor', 'Visor_BaseColor', metallic=.28, roughness=.43)
    for name, material, mesh_names in (
        ('Helmet', helmet, ('Helmet_LOD0',)), ('Visor', visor, ('VisorPivot_LOD0', 'Visiere_LOD0')),
    ):
        equipment = supplied[name]
        items, _ = model_nodes(scene, equipment['Model']['Nodes'], knight)
        model_meshes(scene, equipment, items, dict.fromkeys(mesh_names, material), knight)


def create_horse(scene, visual, supplied, asset_root):
    model = supplied['Model']
    walk = find_clip(supplied['Clips'], 'walk')
    idle = find_clip(supplied['Clips'], 'idle')
    canter = find_clip(supplied['Clips'], supplied.get('Pace', 'canter'))
    rig = scene.node('Warhorse Rig', visual['ID'], rotation=rotation(0, supplied.get('Yaw', 0)), components=(
        {'Type': 'GameSproutRuntime.Animator', 'Skeleton': model['Skeleton'], 'DefaultClip': idle,
         'Speed': 1, 'PlayOnStart': False, 'Loop': True, 'RootMotionMode': 'ExtractOnly', 'Bindings': []},
    ))
    animator = rig['Components'][-1]
    model_items, world_poses = model_nodes(scene, model['Nodes'], rig)
    animator['Bindings'] = [{'Path': source['Path'], 'ObjectFileID': model_items[i]['ID']}
                            for i, source in enumerate(model['Nodes'])]
    model_meshes(scene, supplied, model_items, supplied['MaterialsByNode'], rig)
    sockets = [i for i, source in enumerate(model['Nodes']) if source['Name'] == supplied['SeatBone']]
    seat_position = supplied['SeatPosition']
    parent = visual
    seat_scale = UNIT_SCALE
    seat_rotation = IDENTITY
    if sockets:
        socket = sockets[0]
        parent = model_items[socket]
        socket_position, socket_rotation, socket_scale = world_poses[socket]
        rig_rotation = rig['Components'][0]['Rotation']
        # Preserve the calibrated upright rider pose in the horse's space,
        # while letting the animated saddle carry the rider through each gait.
        model_seat = rotate(inverse(rig_rotation), seat_position)
        seat_offset = tuple(a - b for a, b in zip(model_seat, socket_position))
        seat_scale = tuple(1 / value for value in socket_scale)
        seat_rotation = multiply(inverse(socket_rotation), inverse(rig_rotation))
        seat_position = tuple(a * b for a, b in zip(rotate(inverse(socket_rotation), seat_offset), seat_scale))
    seat = scene.node('Rider Seat', parent['ID'], position=seat_position, scale=seat_scale, rotation=seat_rotation)
    create_knight(scene, asset_root, seat, supplied['Knight'])
    return animator, {'IdleClip': idle, 'WalkClip': walk, 'CanterClip': canter}


def add_gameplay(root, scene, model):
    sun_settings = {
        'Type': 'Directional', 'Color': [1, .91, .77, 1], 'Intensity': 1.62,
        'Shadows': {'Enabled': True, 'Strength': .93, 'Distance': 140, 'Resolution': 2048,
                    'CascadeCount': 4, 'DepthBias': .9, 'NormalBias': 1.1, 'Soft': True,
                    'CacheStaticCasters': True, 'StaticUpdateMode': 'AutomaticIncremental',
                    'FadeStart': .76, 'FilterQuality': 'Medium'},
    }
    scene.node('Afternoon Side Light', rotation=rotation(33, 69), components=(
        {'Type': 'GameSproutRuntime.Light', 'Settings': sun_settings},
    ))
    surface = write_asset(root, 'Characters/Mount.physicmaterial', 'PhysicsMaterial', 19, {
        'Version': 1, 'Friction': 0, 'Restitution': 0,
        'FrictionCombine': 'Minimum', 'RestitutionCombine': 'Minimum',
    })
    mount = scene.node('Mount', position=(0, terrain_height(0, 0) + .09, 0), components=(
        {'Type': 'GameSproutRuntime.Rigidbody', 'Settings': {'MotionType': 'Dynamic', 'Constraints': 56, 'Interpolate': True}},
        {'Type': 'GameSproutRuntime.CapsuleCollider', 'Settings': {'Material': surface, 'Center': (0, .95, 0)},
         'Shape': {'Radius': .46, 'Height': 1.9, 'Axis': 'Y'}},
    ))
    visual = scene.node('Mount Visual', mount['ID'])
    camera = scene.node('Main Camera', position=(0, 3.1, -4.9), rotation=rotation(15, 0), components=(
        {'Type': 'GameSproutRuntime.Camera', 'Settings': {'Projection': 'Perspective',
         'VerticalFovRadians': math.radians(51), 'OrthographicHeight': 10, 'NearPlane': .08, 'FarPlane': 1250,
         'CullingMask': 4294967295}},
    ))
    animator, clips = create_horse(scene, visual, model, root)
    fields = {f'HorseRider::{name}': {'Kind': 'SceneObject', 'Value': {'ObjectFileID': node['ID']}}
              for name, node in (('Camera', camera), ('Visual', visual))}
    fields.update({f'HorseRider::{name}': {'Kind': 'Component', 'Value': {'ComponentFileID': component['ID']}}
                   for name, component in (('Body', mount['Components'][1]), ('Collider', mount['Components'][2]),
                                           ('Animator', animator))})
    fields['HorseRider::CameraDistance'] = {'Kind': 'Number', 'Value': 4.9}
    for name, reference in clips.items():
        fields[f'HorseRider::{name}'] = {'Kind': 'Resource', 'Value': reference}
    mount['Components'].append({'ID': scene.identity(), 'Type': 'GameSproutRuntime.ScriptComponent',
                                'InstanceID': 1, 'ScriptTypeID': SCRIPT_GUID, 'Enabled': True, 'Fields': fields,
                                'Source': {'Guid': SCRIPT_GUID, 'LocalID': 1}})
    for extension, guid in (('h', SCRIPT_GUID), ('cpp', SCRIPT_CPP_GUID)):
        relative = f'Scripts/HorseRider.{extension}'
        path = root / 'Assets' / relative
        if not path.is_file():
            raise ValueError(f'Missing gameplay script: {path}')
        save_defaults(root / 'Assets' / (relative + '.meta'), {'Version': 1, 'Guid': guid, 'ImporterType': 'Script',
                       'Path': relative, 'ImportSettings': {}, 'SubAssets': []})
    return camera


def build(root, model, replace):
    path = root / 'Assets' / SCENE_PATH
    if path.is_file() and not replace:
        raise ValueError(f'{path} exists. Use --replace to regenerate the authored scene and generated assets.')
    scene = Scene()
    camera = add_gameplay(root, scene, model)
    build_landscape(root, scene.node)
    build_sky(root, scene.node)
    lighting = {'Mode': 'Gradient', 'Intensity': 1, 'SkyColor': [.245, .315, .40, 1],
                'EquatorColor': [.165, .186, .215, 1], 'GroundColor': [.123, .098, .071, 1],
                'Atmosphere': {'Enabled': True, 'DrawSky': True, 'ZenithColor': [.062, .225, .455, 1],
                               'HorizonColor': [.38, .55, .68, 1], 'FogColor': [.395, .52, .61, 1],
                               'FogStart': 115, 'FogEnd': 810, 'FogMaxOpacity': .68}}
    write_asset(root, SCENE_PATH, 'Scene', 13, {'Version': 2, 'Name': 'Valley Road', 'MainCameraID': camera['ID'],
                    'NextObjectID': scene.next_id, 'EnvironmentLighting': lighting, 'Objects': scene.objects})
    save_defaults(root / 'Info' / 'GameLogic.json', {'Version': 1, 'Guid': '7ccdbbcafabf4b35b6134d03db4f24e0'})
    save_defaults(root / 'UserSettings' / 'EditorState.json', {'LastScenePath': SCENE_PATH})
    return len(scene.objects)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('descriptor', type=Path)
    parser.add_argument('--replace', action='store_true')
    args = parser.parse_args()
    print(f'{SCENE_PATH}: {build(ROOT, json.loads(args.descriptor.read_text()), args.replace)} objects')
