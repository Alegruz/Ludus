"""Read authored camera/lights through Assimp's C API for private acceptance.

The glTF exporter omits these FBX inputs. Keep this test-only adapter out of the
engine/cooker. Requires a caller-selected Assimp 6 shared library, no download.
"""
import ctypes as c
import math
from pathlib import Path
import numpy as np


class Text(c.Structure):
    _fields_ = [('length', c.c_uint32), ('data', c.c_char * 1024)]


class Vector(c.Structure):
    _fields_ = [('x', c.c_float), ('y', c.c_float), ('z', c.c_float)]
    def array(self): return np.array([self.x, self.y, self.z])


class Node(c.Structure):
    pass


Node._fields_ = [('name', Text), ('transform', c.c_float * 16), ('parent', c.POINTER(Node)),
    ('count', c.c_uint32), ('children', c.POINTER(c.POINTER(Node)))]


class Light(c.Structure):
    _fields_ = [('name', Text), ('kind', c.c_uint32), ('position', Vector), ('direction', Vector),
        ('up', Vector), ('constant', c.c_float), ('linear', c.c_float), ('quadratic', c.c_float),
        ('diffuse', Vector), ('specular', Vector), ('ambient', Vector), ('inner', c.c_float), ('outer', c.c_float)]


class Camera(c.Structure):
    _fields_ = [('name', Text), ('position', Vector), ('up', Vector), ('look', Vector),
        ('fov', c.c_float), ('near', c.c_float), ('far', c.c_float), ('aspect', c.c_float), ('orthographic', c.c_float)]


class Scene(c.Structure):
    _fields_ = [('flags', c.c_uint32), ('root', c.POINTER(Node)), ('meshes', c.c_uint32), ('mesh_data', c.c_void_p),
        ('materials', c.c_uint32), ('material_data', c.c_void_p), ('animations', c.c_uint32), ('animation_data', c.c_void_p),
        ('textures', c.c_uint32), ('texture_data', c.c_void_p), ('light_count', c.c_uint32), ('lights', c.POINTER(c.POINTER(Light))),
        ('camera_count', c.c_uint32), ('cameras', c.POINTER(c.POINTER(Camera)))]


def read(path: Path, library: str):
    api = c.CDLL(library)
    if api.aiGetVersionMajor() != 6: raise ValueError('Acceptance requires Assimp 6 ABI')
    if api.aiGetCompileFlags() & 0x20: raise ValueError('Assimp double-precision ABI is unsupported')
    api.aiImportFile.argtypes = [c.c_char_p, c.c_uint32]
    api.aiImportFile.restype = c.POINTER(Scene)
    api.aiReleaseImport.argtypes = [c.POINTER(Scene)]
    pointer = api.aiImportFile(str(path).encode(), 0)
    if not pointer: raise ValueError('Assimp could not read authored FBX inputs')
    try:
        scene = pointer.contents
        transforms = {}
        def walk(pointer, parent):
            node = pointer.contents
            world = parent @ np.array(node.transform).reshape(4, 4)
            transforms[node.name.data[:node.name.length].decode()] = world
            for index in range(node.count): walk(node.children[index], world)
        walk(scene.root, np.eye(4))
        lights = []
        for index in range(scene.light_count):
            source = scene.lights[index].contents
            name = source.name.data[:source.name.length].decode()
            world = transforms[name]
            if source.kind not in (1, 2, 3): raise ValueError('Unsupported authored FBX light type')
            radiance = source.diffuse.array()
            intensity = float(max(radiance))
            color = radiance / intensity if intensity > 0 else np.ones(3)
            position = world[:3, :3] @ source.position.array() + world[:3, 3]
            direction = world[:3, :3] @ source.direction.array()
            if source.kind != 2: direction /= np.linalg.norm(direction)
            record = {'name': name, 'type': {1: 'directional', 2: 'point', 3: 'spot'}[source.kind], 'color': color.tolist(),
                'intensity': intensity, 'range': 1000000}
            if source.kind == 3: record['spot'] = {'innerConeAngle': float(source.inner), 'outerConeAngle': float(source.outer)}
            pose = np.eye(4); pose[:3, 3] = position; pose[:3, 2] = -direction
            lights.append((record, pose))
        camera = None
        if scene.camera_count:
            source = scene.cameras[0].contents
            name = source.name.data[:source.name.length].decode()
            world = transforms[name]
            eye = world[:3, :3] @ source.position.array() + world[:3, 3]
            forward = world[:3, :3] @ source.look.array(); forward /= np.linalg.norm(forward)
            up = world[:3, :3] @ source.up.array(); up /= np.linalg.norm(up)
            camera = {'name': name, 'eye': eye.tolist(), 'forward': forward.tolist(), 'up': up.tolist(),
                'fov_half_horizontal': float(source.fov), 'near': float(source.near), 'far': float(source.far), 'aspect': float(source.aspect)}
        return lights, camera
    finally:
        api.aiReleaseImport(pointer)
