#!/usr/bin/env python3
"""Prepare private full-geometry renderer acceptance fixtures, not an engine importer.

Requires NumPy/Pillow and Assimp 6 for SunTemple. Never downloads or executes
source .pyscene files. Output and converted source stay in the caller's out/ tree.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess

import numpy as np
from PIL import Image

MAGIC = 0x5253434E
VERSION = 1
TEXTURE_LIMIT = 512


def read_hdr(path):
    with path.open('rb') as file:
        while file.readline().strip():
            pass
        orientation = file.readline().decode().split()
        if orientation[0] != '-Y' or orientation[2] != '+X':
            raise ValueError('Acceptance supports explicit -Y +X Radiance images')
        height, width = int(orientation[1]), int(orientation[3])
        encoded = np.zeros((height, width, 4), dtype=np.uint8)
        for y in range(height):
            header = file.read(4)
            if header[:2] != b'\x02\x02' or int.from_bytes(header[2:], 'big') != width:
                raise ValueError('Expected Radiance scanline RLE')
            for channel in range(4):
                x = 0
                while x < width:
                    code = file.read(1)[0]
                    if code > 128:
                        count = code - 128
                        encoded[y, x:x+count, channel] = file.read(1)[0]
                    else:
                        count = code
                        encoded[y, x:x+count, channel] = list(file.read(count))
                    if count == 0 or x + count > width:
                        raise ValueError('Invalid HDR run')
                    x += count
    scale = np.ldexp(np.ones((height, width)), encoded[:, :, 3].astype(np.int32) - 136)
    rgb = encoded[:, :, :3].astype(np.float64) * scale[:, :, None]
    result = np.ones((height, width, 4), dtype='<f4')
    result[:, :, :3] = rgb
    if not np.isfinite(result).all() or rgb.max() > 1e12:
        raise ValueError('HDR source exceeds declared finite HDR source range')
    return result


def accessor(document, buffers, index):
    description = document['accessors'][index]
    if 'sparse' in description:
        raise ValueError('Sparse accessor requires an explicit conversion')
    view = document['bufferViews'][description['bufferView']]
    types = {5120: 'i1', 5121: 'u1', 5122: '<i2', 5123: '<u2', 5125: '<u4', 5126: '<f4'}
    components = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[description['type']]
    dtype = np.dtype(types[description['componentType']])
    stride = view.get('byteStride', components * dtype.itemsize)
    array = np.ndarray((description['count'], components), dtype=dtype,
        buffer=buffers[view['buffer']], offset=view.get('byteOffset', 0) + description.get('byteOffset', 0),
        strides=(stride, dtype.itemsize)).copy()
    if description.get('normalized'):
        array = array.astype(np.float32) / np.iinfo(dtype).max
        if dtype.kind == 'i':
            array = np.maximum(array, -1)
    return array


def matrix(node):
    if 'matrix' in node:
        return np.asarray(node['matrix']).reshape((4, 4), order='F')
    x, y, z, w = node.get('rotation', [0, 0, 0, 1])
    result = np.eye(4)
    result[:3, :3] = [[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
        [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
        [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]]
    result[:3, :3] *= node.get('scale', [1, 1, 1])
    result[:3, 3] = node.get('translation', [0, 0, 0])
    return result


def prepare(source, output, kind, hdr, original, fbx_library=None):
    document = json.loads(source.read_text())
    buffers = [(source.parent / item['uri']).read_bytes() for item in document['buffers']]
    source_root = original if kind == 'suntemple' else source.parent
    texture_cache = {}
    def texture(info, default=(255, 255, 255, 255)):
        if info is None:
            return np.array([[default]], dtype=np.uint8)
        image = document['images'][document['textures'][info['index']]['source']]
        uri = image['uri'].replace('\\', '/')
        path = source_root / uri
        if path not in texture_cache:
            decoded = Image.open(path).convert('RGBA')
            # Explicit acceptance resolution; every material and primitive is retained.
            decoded.thumbnail((TEXTURE_LIMIT, TEXTURE_LIMIT), Image.Resampling.LANCZOS)
            texture_cache[path] = np.array(decoded, dtype=np.uint8)
        return texture_cache[path]
    materials = []
    for description in document.get('materials', []):
        pbr = description.get('pbrMetallicRoughness', {})
        base = texture(pbr.get('baseColorTexture'))
        normal = texture(description.get('normalTexture'), (128, 128, 255, 255))
        if kind == 'suntemple':
            authored_orm = description.get('extensions', {}).get('KHR_materials_specular', {}).get('specularTexture')
            if authored_orm is None:
                raise ValueError('Missing authored ORM source binding')
            orm = texture(authored_orm)
            roughness = metallic = 1.0
        else:
            orm = texture(pbr.get('metallicRoughnessTexture'))
            roughness = pbr.get('roughnessFactor', 1)
            metallic = pbr.get('metallicFactor', 1)
        size = (max(normal.shape[1], orm.shape[1]), max(normal.shape[0], orm.shape[0]))
        normal = np.array(Image.fromarray(normal).resize(size, Image.Resampling.BILINEAR))
        orm = np.array(Image.fromarray(orm).resize(size, Image.Resampling.BILINEAR))
        packed = normal.copy()
        if kind == 'suntemple':
            packed[:, :, 1] = 255 - packed[:, :, 1]
        packed[:, :, 2] = orm[:, :, 1]
        packed[:, :, 3] = orm[:, :, 2]
        emission = texture(description.get('emissiveTexture'))
        factor = description.get('emissiveFactor', [0, 0, 0])
        if kind == 'suntemple':
            # Source SunTemple.pyscene explicitly multiplies emissive factors by 1000.
            factor = [value * 1000 for value in factor]
        modes = {'OPAQUE': 0, 'MASK': 1, 'BLEND': 2}
        alpha = modes[description.get('alphaMode', 'OPAQUE')]
        uv_sets = {info.get('texCoord', 0) for info in [pbr.get('baseColorTexture'), description.get('normalTexture'),
            pbr.get('metallicRoughnessTexture'), description.get('emissiveTexture')] if info}
        if len(uv_sets) > 1:
            raise ValueError('Independent UV routing requires explicit texture rebaking')
        materials.append((base, packed, emission, pbr.get('baseColorFactor', [1, 1, 1, 1]),
            metallic, roughness, description.get('normalTexture', {}).get('scale', 1),
            description.get('alphaCutoff', .5), factor, alpha, next(iter(uv_sets), 0)))
    meshes = []
    mesh_map = {}
    attribute_counts = {}
    tangent_fallback_vertices = 0
    for mesh_index, mesh in enumerate(document['meshes']):
        mesh_map[mesh_index] = []
        for primitive in mesh['primitives']:
            if primitive.get('mode', 4) != 4:
                raise ValueError('Non-triangle source primitive')
            attributes = primitive['attributes']
            for name in attributes:
                attribute_counts[name] = attribute_counts.get(name, 0) + 1
            positions = accessor(document, buffers, attributes['POSITION']).astype('<f4')
            count = len(positions)
            indices = accessor(document, buffers, primitive['indices']).astype('<u4').reshape(-1)
            normals = accessor(document, buffers, attributes['NORMAL']).astype('<f4')
            uv0 = accessor(document, buffers, attributes['TEXCOORD_0']).astype('<f4') if 'TEXCOORD_0' in attributes else np.zeros((count, 2), dtype='<f4')
            uv1 = accessor(document, buffers, attributes['TEXCOORD_1']).astype('<f4') if 'TEXCOORD_1' in attributes else uv0
            tangents = accessor(document, buffers, attributes['TANGENT']).astype('<f4') if 'TANGENT' in attributes else np.zeros((count, 4), dtype='<f4')
            if 'TANGENT' in attributes:
                normal_lengths = np.linalg.norm(normals, axis=1)
                if np.any(normal_lengths == 0): raise ValueError('Zero authored normal')
                normalized = normals / normal_lengths[:, None]
                orthogonal = tangents[:, :3] - normalized * np.sum(normalized * tangents[:, :3], axis=1)[:, None]
                unavailable = np.linalg.norm(orthogonal, axis=1) <= 1e-8
                tangent_fallback_vertices += int(unavailable.sum())
                tangents[unavailable] = 0  # Explicit sentinel; retain every vertex/triangle.
            colors = accessor(document, buffers, attributes['COLOR_0']).astype('<f4') if 'COLOR_0' in attributes else np.ones((count, 4), dtype='<f4')
            if colors.shape[1] == 3:
                colors = np.concatenate([colors, np.ones((count, 1), dtype='<f4')], axis=1)
            mesh_map[mesh_index].append(len(meshes))
            meshes.append((positions, indices, normals, uv0, uv1, tangents, colors, primitive.get('material', 0), 'TANGENT' in attributes))
    items = []
    lights = []
    authored_lights = document.get('extensions', {}).get('KHR_lights_punctual', {}).get('lights', [])
    bounds_min = np.full(3, np.inf)
    bounds_max = -bounds_min
    def walk(index, parent):
        nonlocal bounds_min, bounds_max
        node = document['nodes'][index]
        world = parent @ matrix(node)
        if 'mesh' in node:
            for mesh in mesh_map[node['mesh']]:
                items.append((mesh, world))
                positions = meshes[mesh][0]
                points = positions @ world[:3, :3].T + world[:3, 3]
                bounds_min = np.minimum(bounds_min, points.min(axis=0))
                bounds_max = np.maximum(bounds_max, points.max(axis=0))
        light = node.get('extensions', {}).get('KHR_lights_punctual', {}).get('light')
        if light is not None:
            lights.append((authored_lights[light], world))
        for child in node.get('children', []):
            walk(child, world)
    for index in document['scenes'][document.get('scene', 0)]['nodes']:
        walk(index, np.eye(4))
    authored_camera = None
    if kind == 'suntemple':
        if fbx_library is None: raise ValueError('SunTemple requires --assimp-library to preserve FBX camera/lights')
        from resource_fbx_inputs import read
        lights, authored_camera = read(original/'SunTemple.fbx', fbx_library)
    environment = read_hdr(hdr)
    center = (bounds_min + bounds_max) / 2
    extent = bounds_max - bounds_min
    distance = max(extent)
    eye = center + np.array([.75, .50, .85]) * distance
    target = center
    if kind == 'sponza':
        eye = np.array([8.5, 1.8, .2])
        target = np.array([-7.0, 4.0, .2])
    if authored_camera:
        eye = np.array(authored_camera['eye'])
        target = eye + np.array(authored_camera['forward'])
    forward = target - eye
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, authored_camera['up'] if authored_camera else [0, 1, 0]); right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    view = np.eye(4); view[:3, :3] = [right, up, -forward]; view[:3, 3] = -view[:3, :3] @ eye
    near, far = distance * .001, distance * 10
    projection = np.zeros((4, 4)); projection[0, 0] = projection[1, 1] = 1 / math.tan(authored_camera['fov_half_horizontal'] if authored_camera else math.radians(50)/2)
    projection[2, 2] = far / (near-far); projection[2, 3] = near * far / (near-far); projection[3, 2] = -1
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('wb') as file:
        def words(*values): file.write(struct.pack('<'+'I'*len(values), *values))
        def floats(values): file.write(np.asarray(values, dtype='<f4').tobytes())
        words(MAGIC, VERSION, len(meshes), len(materials), len(items), environment.shape[1], environment.shape[0], len(lights))
        floats((projection @ view).flatten(order='F')); floats(eye)
        floats(environment)
        for light, world in lights:
            words({'directional': 0, 'point': 1, 'spot': 2}[light['type']])
            floats(light.get('color', [1, 1, 1])); floats([light.get('intensity', 1)])
            floats(world[:3, 3]); floats(-world[:3, 2]); floats([light.get('range', distance * 10),
                math.cos(light.get('spot', {}).get('innerConeAngle', 0)), math.cos(light.get('spot', {}).get('outerConeAngle', math.pi/4))])
        for base, packed, emission, tint, metallic, roughness, scale, cutoff, factor, alpha, uv_set in materials:
            words(base.shape[1], base.shape[0], packed.shape[1], packed.shape[0], emission.shape[1], emission.shape[0], alpha, uv_set)
            floats(tint); floats([metallic, max(.045, roughness), scale, cutoff]); floats(factor)
            for texture in (base, packed, emission): file.write(texture.tobytes())
        for positions, indices, normals, uv0, uv1, tangents, colors, material, has_tangent in meshes:
            words(len(positions), len(indices), material, int(has_tangent))
            for values in (positions, normals, uv0, uv1, tangents, colors): floats(values)
            file.write(indices.tobytes())
        for mesh, world in items:
            words(mesh); floats(world[:3, :].flatten(order='F'))
    report = {'scene': kind, 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'fixture_sha256': hashlib.sha256(output.read_bytes()).hexdigest(), 'primitives': len(meshes), 'materials': len(materials),
        'instances': len(items), 'stored_vertices': sum(len(m[0]) for m in meshes),
        'stored_triangles': sum(len(m[1])//3 for m in meshes), 'instanced_triangles': sum(len(meshes[i][1])//3 for i, _ in items),
        'attributes': attribute_counts, 'tangent_fallback_vertices': tangent_fallback_vertices, 'punctual_lights': len(lights), 'texture_max_dimension': TEXTURE_LIMIT,
        'hdr_dimensions': list(environment.shape[:2]), 'hdr_max_radiance': float(environment[:, :, :3].max()),
        'bounds': [bounds_min.tolist(), bounds_max.tolist()], 'camera': eye.tolist(), 'camera_source': 'FBX authored' if authored_camera else 'explicit acceptance rig',
        'light_profile': 'Assimp RGB intensity interpreted as lux/candela, bounded renderer attenuation' if kind == 'suntemple' else 'glTF KHR_lights_punctual, authored zero intensities preserved',
        'limitations': ['packed map shared UV/sampler; source AO is not applied to reference transport',
            'RGB single-scatter metallic/roughness; physical glass and layered coatings remain outside this profile',
            'SunTemple source FBX contains no tangent layers; UV-derived fallback' if kind == 'suntemple' else 'source authored tangents retained']}
    output.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--assimp', default='assimp')
    parser.add_argument('--assimp-library', required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    sun = args.resources / 'SunTemple_v4' / 'SunTemple'
    converted = args.output / 'suntemple' / 'SunTemple.gltf'
    converted.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([args.assimp, 'export', str(sun/'SunTemple.fbx'), str(converted), '-fgltf2'], check=True)
    prepare(converted, args.output/'suntemple.rscn', 'suntemple', sun/'SunTemple_Skybox.hdr', sun, args.assimp_library)
    sponza = args.resources/'main_sponza'
    hdrs = list(sponza.rglob('*.hdr'))
    if len(hdrs) != 1: raise ValueError('Sponza acceptance requires its unique supplied HDR')
    prepare(sponza/'NewSponza_Main_glTF_003.gltf', args.output/'sponza.rscn', 'sponza', hdrs[0], sponza)


if __name__ == '__main__':
    main()
