#!/usr/bin/env python3
"""Cook the explicit staircase2 PBRT/PLY/TGA subset into a bounded Ludus sample bundle.

Thanks to Matt Pharr, Wenzel Jakob and Greg Humphreys, pbrt-v4 Input File
Format (https://pbrt.org/fileformat-v4), Transformations, Cameras, Materials
and Shapes, for camera-from-world and scoped graphics-state semantics.
This is an original offline converter, not a PBRT renderer or copied parser.
Modern Hall geometry/textures are by NewSee2l035, CC BY 3.0; see the input LICENSE.
"""

from __future__ import annotations
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import re
import struct

IDENTITY = [
    1.0,
    0.0,
    0.0,
    0.0,
    0.0,
    1.0,
    0.0,
    0.0,
    0.0,
    0.0,
    1.0,
    0.0,
    0.0,
    0.0,
    0.0,
    1.0,
]
COMMANDS = {
    "Integrator",
    "Transform",
    "Sampler",
    "PixelFilter",
    "Film",
    "Camera",
    "WorldBegin",
    "Texture",
    "MakeNamedMaterial",
    "NamedMaterial",
    "Material",
    "Shape",
    "AreaLightSource",
    "AttributeBegin",
    "AttributeEnd",
    "TransformBegin",
    "TransformEnd",
    "WorldEnd",
}


def asset_path(source, name):
    relative = Path(name)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError("asset path escapes source directory")
    path = (source / relative).resolve()
    if not path.is_relative_to(source.resolve()):
        raise ValueError("asset symlink escapes source directory")
    return path


def bounded_bytes(path, maximum=64 * 1024 * 1024):
    if path.stat().st_size > maximum:
        raise ValueError("source file byte limit")
    with path.open("rb") as stream:
        data = stream.read(maximum + 1)
    if len(data) > maximum:
        raise ValueError("source file byte limit")
    return data


def finite(values):
    values = [float(x) for x in values]
    if not all(math.isfinite(x) for x in values):
        raise ValueError("nonfinite source number")
    return values


def parse_scene(path):
    text = bounded_bytes(path, 1024 * 1024).decode("utf-8")
    tokens = re.findall(
        r'"(?:[^"\\]|\\.)*"|\[|\]|[^\s\[\]"]+', re.sub(r"#[^\n]*", "", text)
    )
    commands = []
    i = 0
    while i < len(tokens):
        command = tokens[i]
        i += 1
        if command not in COMMANDS:
            raise ValueError(f"unsupported directive {command}")
        arguments = []
        while i < len(tokens) and tokens[i] not in COMMANDS:
            if tokens[i] == "[":
                i += 1
                values = []
                while i < len(tokens) and tokens[i] != "]":
                    values.append(tokens[i])
                    i += 1
                if i == len(tokens):
                    raise ValueError("unterminated array")
                arguments.append(values)
                i += 1
            else:
                arguments.append(tokens[i])
                i += 1
        names, params = [], {}
        for arg in arguments:
            if params and next(reversed(params.values())) is None:
                key = next(reversed(params))
                values = arg if isinstance(arg, list) else [arg]
                params[key] = [
                    json.loads(x) if x.startswith('"') else x for x in values
                ]
            elif isinstance(arg, str) and arg.startswith('"'):
                value = json.loads(arg)
                if " " not in value and not params:
                    names.append(value)
                else:
                    if value in params:
                        raise ValueError("duplicate parameter")
                    params[value] = None
            else:
                names.append(arg)
        allowed = {
            "Integrator": {
                "integer maxdepth",
                "string lightsampler",
                "float trainingBudgetRatio",
            },
            "Sampler": {"integer pixelsamples"},
            "PixelFilter": set(),
            "Film": {"string filename", "integer xresolution", "integer yresolution"},
            "Camera": {"float fov"},
            "Texture": {"string filename", "string filter"},
            "MakeNamedMaterial": {
                "string type",
                "rgb reflectance",
                "texture reflectance",
                "float uroughness",
                "float vroughness",
                "bool remaproughness",
                "rgb eta",
                "rgb k",
            },
            "Material": {"rgb reflectance"},
            "AreaLightSource": {"rgb L"},
            "Shape": {
                "string filename",
                "float radius",
                "point3 P",
                "normal N",
                "point2 uv",
                "integer indices",
            },
        }.get(command, set())
        if set(params) - allowed or any(value is None for value in params.values()):
            raise ValueError(f"unsupported or missing parameter for {command}")
        commands.append((command, names, params))
    textures, materials, shapes = {}, {}, []
    state = {"transform": IDENTITY[:], "material": None, "emission": None}
    stack = []
    camera = None
    fov = None
    world = False
    for command, names, params in commands:
        if command == "Transform":
            values = finite(names[0])
            if (
                len(values) != 16
                or values[3:12:4] != [0.0, 0.0, 0.0]
                or values[15] != 1
            ):
                raise ValueError("non-affine transform")
            state["transform"] = values
        elif command == "Camera":
            if names != ["perspective"]:
                raise ValueError("only perspective camera is supported")
            if camera is not None:
                raise ValueError("duplicate camera")
            camera = state["transform"][:]
            fov = finite(params["float fov"])[0]
            if not 0 < fov < 180:
                raise ValueError("camera field of view")
        elif command == "WorldBegin":
            world = True
            state["transform"] = IDENTITY[:]
        elif command == "Texture":
            if names[1:] != ["spectrum", "imagemap"]:
                raise ValueError("unsupported texture")
            if names[0] in textures or params.get("string filter", ["trilinear"]) != [
                "trilinear"
            ]:
                raise ValueError("unsupported texture profile")
            textures[names[0]] = params["string filename"][0]
        elif command == "MakeNamedMaterial":
            if names[0] in materials:
                raise ValueError("duplicate material")
            materials[names[0]] = params
        elif command == "NamedMaterial":
            state["material"] = materials[names[0]]
        elif command == "Material":
            state["material"] = {**params, "string type": names}
        elif command == "AreaLightSource":
            if names != ["diffuse"]:
                raise ValueError("unsupported emitter")
            state["emission"] = finite(params["rgb L"])
            if len(state["emission"]) != 3 or any(
                v < 0 or v > 10000 for v in state["emission"]
            ):
                raise ValueError("emission profile")
        elif command in ("AttributeBegin", "TransformBegin"):
            stack.append((command, copy.deepcopy(state)))
        elif command in ("AttributeEnd", "TransformEnd"):
            if not stack:
                raise ValueError("unbalanced state stack")
            start, previous = stack.pop()
            if start.replace("Begin", "End") != command:
                raise ValueError("mismatched state stack")
            if start == "TransformBegin":
                state["transform"] = previous["transform"]
            else:
                state = previous
        elif command == "Shape":
            if not world or state["material"] is None:
                raise ValueError("shape outside world or without material")
            shapes.append({**copy.deepcopy(state), "kind": names[0], "params": params})
        elif command == "WorldEnd":
            world = False
        elif command in ("Integrator", "Sampler", "PixelFilter", "Film"):
            pass  # Explicitly replaced by the documented L6 capture profile.
    if stack or camera is None or fov is None or not shapes:
        raise ValueError("incomplete scene")
    return camera, fov, textures, materials, shapes


def read_ply(path):
    data = bounded_bytes(path)
    boundary = data.find(b"end_header\n")
    if boundary < 0 or boundary > 4096:
        raise ValueError("invalid PLY header")
    header = data[:boundary].decode("ascii").splitlines()
    payload = memoryview(data)[boundary + 11 :]
    if header[:2] != ["ply", "format binary_little_endian 1.0"]:
        raise ValueError("unsupported PLY encoding")
    props = []
    vertices = faces = None
    element = None
    index_format = None
    for line in header[2:]:
        fields = line.split()
        if not fields or fields[0] == "comment":
            continue
        if fields[0] == "element":
            element = fields[1]
            if element == "vertex":
                vertices = int(fields[2])
            elif element == "face":
                faces = int(fields[2])
            else:
                raise ValueError("unsupported PLY element")
        elif fields[0] == "property" and element == "vertex":
            if fields[1] != "float":
                raise ValueError("unsupported vertex property")
            props.append(fields[2])
        elif fields[0] == "property" and element == "face":
            if fields[1:3] != ["list", "uint8"] and fields[1:3] != ["list", "uchar"]:
                raise ValueError("unsupported face list")
            if fields[3] not in ("int", "uint"):
                raise ValueError("unsupported index type")
            index_format = "i" if fields[3] == "int" else "I"
    if (
        len(set(props)) != len(props)
        or not {"x", "y", "z"}.issubset(props)
        or set(props) - {"x", "y", "z", "nx", "ny", "nz", "u", "v", "s", "t"}
    ):
        raise ValueError("unsupported PLY vertex schema")
    if set(props) & {"nx", "ny", "nz"} and not {"nx", "ny", "nz"}.issubset(props):
        raise ValueError("incomplete PLY normals")
    if (
        vertices is None
        or faces is None
        or index_format is None
        or not 0 < vertices <= 1000000
        or not 0 < faces <= 1000000
    ):
        raise ValueError("PLY limits")
    row = struct.Struct("<" + "f" * len(props))
    offset = 0
    points = []
    for _ in range(vertices):
        values = finite(row.unpack_from(payload, offset))
        offset += row.size
        v = dict(zip(props, values))
        points.append(
            (
                tuple(v[x] for x in ("x", "y", "z")),
                tuple(v[x] for x in ("nx", "ny", "nz")) if "nx" in v else None,
                (v.get("u", v.get("s", 0)), 1 - v.get("v", v.get("t", 0))),
            )
        )
    triangles = []
    for _ in range(faces):
        count = payload[offset]
        offset += 1
        if count not in (3, 4):
            raise ValueError("only triangle/convex quad faces are supported")
        indices = struct.unpack_from("<" + index_format * count, payload, offset)
        offset += 4 * count
        if any(x < 0 or x >= vertices for x in indices):
            raise ValueError("PLY index range")
        triangles.extend(
            (indices[0], indices[i], indices[i + 1]) for i in range(1, count - 1)
        )
    if offset != len(payload):
        raise ValueError("unexpected PLY trailing bytes")
    return points, triangles


def read_tga(path):
    data = bounded_bytes(path)
    header = struct.unpack_from("<BBBHHBHHHHBB", data)
    ident, cmap, kind, _, _, _, _, _, width, height, bits, flags = header
    if (
        cmap
        or kind != 2
        or bits not in (24, 32)
        or flags & 0xC0
        or not 0 < width <= 4096
        or not 0 < height <= 4096
    ):
        raise ValueError("only bounded uncompressed true-color TGA is supported")
    if width * height * 4 > 16 * 1024 * 1024:
        raise ValueError("decoded texture byte limit")
    step = bits // 8
    offset = 18 + ident
    if len(data) < offset + width * height * step:
        raise ValueError("truncated TGA")
    output = bytearray(width * height * 4)
    for y in range(height):
        for x in range(width):
            sx = width - 1 - x if flags & 0x10 else x
            sy = y if flags & 0x20 else height - 1 - y
            index = offset + (sy * width + sx) * step
            b, g, r = data[index : index + 3]
            output[(y * width + x) * 4 : (y * width + x + 1) * 4] = bytes(
                (r, g, b, data[index + 3] if step == 4 else 255)
            )
    if any(output[x] != 255 for x in range(3, len(output), 4)):
        raise ValueError("alpha requires a separate conversion profile")
    return width, height, output


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def mul(a, b):
    return tuple(x * b for x in a)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def unit(a):
    length = math.sqrt(dot(a, a))
    if length < 1e-12:
        raise ValueError("degenerate geometry")
    return mul(a, 1 / length)


def point(matrix, p):
    return tuple(
        sum(matrix[4 * c + r] * p[c] for c in range(3)) + matrix[12 + r]
        for r in range(3)
    )


def vector(matrix, p):
    return tuple(sum(matrix[4 * c + r] * p[c] for c in range(3)) for r in range(3))


def identity_item(mesh, material):
    return (mesh, material, [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0])


def rectangle(points, triangles):
    positions = [v[0] for v in points]
    low = [min(v[i] for v in positions) for i in range(3)]
    high = [max(v[i] for v in positions) for i in range(3)]
    axes = [i for i in range(3) if high[i] - low[i] > 1e-5]
    if len(axes) != 2:
        raise ValueError("emissive PLY is not an axis-aligned planar rectangle")
    normal = unit(
        cross(
            sub(positions[triangles[0][1]], positions[triangles[0][0]]),
            sub(positions[triangles[0][2]], positions[triangles[0][0]]),
        )
    )
    u = [0.0, 0.0, 0.0]
    v = [0.0, 0.0, 0.0]
    u[axes[0]] = (high[axes[0]] - low[axes[0]]) / 2
    v[axes[1]] = (high[axes[1]] - low[axes[1]]) / 2
    if dot(cross(u, v), normal) < 0:
        v = mul(v, -1)
    area = sum(
        math.sqrt(dot(n, n)) / 2
        for a, b, c in triangles
        for n in [
            cross(sub(positions[b], positions[a]), sub(positions[c], positions[a]))
        ]
    )
    if abs(area - 4 * math.sqrt(dot(u, u) * dot(v, v))) > max(1e-5, area * 1e-4):
        raise ValueError("emitter is not a complete rectangle")
    return [*u, *v, *normal, *[(a + b) / 2 for a, b in zip(low, high)]]


def clip(poly, axis, limit, greater):
    result = []
    for previous, current in zip(poly[-1:] + poly[:-1], poly):
        a, b = previous[2][axis], current[2][axis]
        inside_a = a >= limit if greater else a <= limit
        inside_b = b >= limit if greater else b <= limit
        if inside_a != inside_b:
            t = (limit - a) / (b - a)
            result.append(
                tuple(
                    tuple(x + t * (y - x) for x, y in zip(previous[k], current[k]))
                    for k in range(3)
                )
            )
        if inside_b:
            result.append(current)
    return result


def atlas_triangles(vertices, atlas_width, atlas_height):
    uv = [v[2] for v in vertices]
    output = []
    cells_x = math.floor(max(v[0] for v in uv)) - math.floor(min(v[0] for v in uv)) + 1
    cells_y = math.floor(max(v[1] for v in uv)) - math.floor(min(v[1] for v in uv)) + 1
    if cells_x * cells_y > 256:
        raise ValueError("wallpaper repeat clipping limit")
    for x in range(
        math.floor(min(v[0] for v in uv)), math.floor(max(v[0] for v in uv)) + 1
    ):
        for y in range(
            math.floor(min(v[1] for v in uv)), math.floor(max(v[1] for v in uv)) + 1
        ):
            poly = vertices
            for axis, limit, greater in (
                (0, x, True),
                (0, x + 1, False),
                (1, y, True),
                (1, y + 1, False),
            ):
                if not poly:
                    break
                poly = clip(poly, axis, limit, greater)
            for i in range(1, len(poly) - 1):
                triangle = [poly[0], poly[i], poly[i + 1]]
                if (
                    dot(
                        cross(
                            sub(triangle[1][0], triangle[0][0]),
                            sub(triangle[2][0], triangle[0][0]),
                        ),
                        cross(
                            sub(triangle[1][0], triangle[0][0]),
                            sub(triangle[2][0], triangle[0][0]),
                        ),
                    )
                    < 1e-20
                ):
                    continue
                output.append(
                    [
                        (
                            p,
                            n,
                            (
                                (2 + (s - x) * 512) / atlas_width,
                                (2 + (t - y) * 512) / atlas_height,
                            ),
                        )
                        for p, n, (s, t) in triangle
                    ]
                )
    return output


def cook(source, output):
    source = source.resolve()
    camera, fov, texture_names, material_names, shapes = parse_scene(
        source / "scene-v4.pbrt"
    )
    inputs = {"scene-v4.pbrt", "LICENSE.txt"}
    images = {}
    for name, path in texture_names.items():
        inputs.add(path)
        images[name] = read_tga(asset_path(source, path))
        if sum(len(image[2]) for image in images.values()) > 32 * 1024 * 1024:
            raise ValueError("decoded texture byte limit")
    wallpaper_name = next(
        name for name, path in texture_names.items() if path.endswith("Wallpaper.tga")
    )
    w, h, wallpaper = images[wallpaper_name]
    if (w, h) != (512, 512):
        raise ValueError("staircase2 wallpaper profile requires 512x512")
    aw, ah = 544, 516
    atlas = bytearray(aw * ah * 4)
    for y in range(516):
        for x in range(516):
            src = (((y - 2) % 512) * 512 + (x - 2) % 512) * 4
            dst = (y * aw + x) * 4
            atlas[dst : dst + 4] = wallpaper[src : src + 4]
    colors = []

    def solid(color):
        color = tuple(color)
        if len(color) != 3 or any(v < 0 or v > 1 for v in color):
            raise ValueError("base color profile")
        if color not in colors:
            colors.append(color)
        index = colors.index(color)
        if index >= 16:
            raise ValueError("solid atlas limit")
        srgb = lambda v: round(
            255 * (12.92 * v if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055)
        )
        rgba = bytes([min(255, max(0, srgb(v))) for v in color] + [255])
        for y in range(index * 16, (index + 1) * 16):
            for x in range(520, 536):
                atlas[(y * aw + x) * 4 : (y * aw + x + 1) * 4] = rgba
        return ((528.5) / aw, (index * 16 + 8.5) / ah)

    meshes = [{"vertices": [], "indices": []} for _ in range(4)]
    textures = [
        (aw, ah, atlas),
        images[next(k for k, v in texture_names.items() if v.endswith("wood5.tga"))],
        images[next(k for k, v in texture_names.items() if v.endswith("Tiles.tga"))],
    ]
    # Diffuse, wood-coated approximation, tile-coated approximation, conductor.
    materials = [
        (0, 0.0, 1.0, (0, 0, 0)),
        (1, 0.0, math.sqrt(0.1), (0, 0, 0)),
        (2, 0.0, math.sqrt(0.01), (0, 0, 0)),
        (0, 1.0, math.sqrt(0.1), (0, 0, 0)),
    ]
    emissions = []
    emitter_items = []
    source_triangles = 0
    source_meshes = 0
    degenerate_triangles = 0
    cooked_vertices = 0
    for shape in shapes:
        kind, params = shape["kind"], shape["params"]
        if kind == "plymesh":
            path = params["string filename"][0]
            inputs.add(path)
            points, triangles = read_ply(asset_path(source, path))
            source_meshes += 1
            source_triangles += len(triangles)
        elif kind == "trianglemesh":
            positions = finite(params["point3 P"])
            normals = finite(params["normal N"])
            uv = finite(params["point2 uv"])
            points = [
                (
                    tuple(positions[i : i + 3]),
                    tuple(normals[i : i + 3]),
                    (uv[i // 3 * 2], 1 - uv[i // 3 * 2 + 1]),
                )
                for i in range(0, len(positions), 3)
            ]
            indices = [int(v) for v in params["integer indices"]]
            triangles = [tuple(indices[i : i + 3]) for i in range(0, len(indices), 3)]
            source_triangles += len(triangles)
        elif kind == "disk":
            points, triangles = [], []
        else:
            raise ValueError(f"unsupported shape {kind}")
        emission = shape["emission"]
        if emission is not None:
            if tuple(emission) not in emissions:
                emissions.append(tuple(emission))
            material = 4 + emissions.index(tuple(emission))
            if kind == "disk":
                radius = finite(params["float radius"])[0]
                if not 0 < radius <= 1:
                    raise ValueError("disk radius profile")
                transform = shape["transform"]
                affine = [
                    *mul(vector(transform, (1, 0, 0)), radius),
                    *mul(vector(transform, (0, 1, 0)), radius),
                    *vector(transform, (0, 0, 1)),
                    *point(transform, (0, 0, 0)),
                ]
                emitter_items.append((5, material, affine))
            elif kind == "plymesh":
                if shape["transform"] != IDENTITY:
                    raise ValueError("transformed rectangle profile not supported")
                emitter_items.append((4, material, rectangle(points, triangles)))
            else:
                if len(points) != 4 or triangles != [(0, 1, 2), (0, 2, 3)]:
                    raise ValueError("unsupported emissive quad")
                p = [point(shape["transform"], x[0]) for x in points]
                u = mul(sub(p[1], p[0]), 0.5)
                v = mul(sub(p[3], p[0]), 0.5)
                if (
                    max(
                        abs(x - y)
                        for x, y in zip(add(p[0], add(mul(u, 2), mul(v, 2))), p[2])
                    )
                    > 1e-4
                ):
                    raise ValueError("non-parallelogram emitter")
                emitter_items.append(
                    (
                        4,
                        material,
                        [*u, *v, *unit(cross(u, v)), *mul(add(p[0], p[2]), 0.5)],
                    )
                )
            continue
        material = shape["material"]
        type_ = material["string type"][0]
        if type_ == "conductor":
            if (
                material.get("bool remaproughness") != ["false"]
                or finite(material.get("float uroughness", [])) != [0.1]
                or finite(material.get("float vroughness", [])) != [0.1]
            ):
                raise ValueError("conductor roughness profile")
            eta, k = finite(material["rgb eta"]), finite(material["rgb k"])
            if len(eta) != 3 or len(k) != 3 or any(v < 0 for v in eta + k):
                raise ValueError("conductor eta/k profile")
            color = [
                ((e - 1) ** 2 + a * a) / ((e + 1) ** 2 + a * a) for e, a in zip(eta, k)
            ]
            group = 3
            fixed_uv = solid(color)
        elif type_ in ("diffuse", "coateddiffuse"):
            texture = material.get("texture reflectance", [None])[0]
            group = (
                1
                if texture_names.get(texture, "").endswith("wood5.tga")
                else 2
                if texture_names.get(texture, "").endswith("Tiles.tga")
                else 0
            )
            if texture is not None and texture not in images:
                raise ValueError("unknown reflectance texture")
            if texture is not None and group == 0 and texture != wallpaper_name:
                raise ValueError("diffuse texture profile")
            if type_ == "coateddiffuse":
                expected = 0.1 if group == 1 else 0.01 if group == 2 else None
                if (
                    expected is None
                    or material.get("bool remaproughness") != ["false"]
                    or finite(material.get("float uroughness", [])) != [expected]
                    or finite(material.get("float vroughness", [])) != [expected]
                ):
                    raise ValueError("coated diffuse roughness profile")
            elif group != 0:
                raise ValueError("diffuse material profile")
            fixed_uv = (
                None
                if texture
                else solid(finite(material.get("rgb reflectance", [0.5, 0.5, 0.5])))
            )
        else:
            raise ValueError(f"unsupported material {type_}")
        transform = shape["transform"]
        if transform != IDENTITY:
            raise ValueError("transformed room mesh profile not supported")
        mesh = meshes[group]
        for triangle in triangles:
            vertices = [points[i] for i in triangle]
            geometric = cross(
                sub(vertices[1][0], vertices[0][0]), sub(vertices[2][0], vertices[0][0])
            )
            if dot(geometric, geometric) < 1e-24:
                degenerate_triangles += 1
                continue
            normal = unit(geometric)
            vertices = [(p, unit(n) if n else normal, uv) for p, n, uv in vertices]
            if fixed_uv:
                pieces = [[(p, n, fixed_uv) for p, n, _ in vertices]]
            elif group == 0:
                pieces = atlas_triangles(vertices, aw, ah)
            else:
                pieces = [vertices]
            for piece in pieces:
                cooked_vertices += 3
                if cooked_vertices > 500000:
                    raise ValueError("expanded vertex limit")
                mesh["indices"].extend(
                    range(len(mesh["vertices"]), len(mesh["vertices"]) + 3)
                )
                mesh["vertices"].extend(piece)
    materials += [(0xFFFFFFFF, 0.0, 1.0, emission) for emission in emissions]
    meshes.append(
        {
            "vertices": [
                ((x, y, 0), (0, 0, 1), (0, 0))
                for x, y in [(-1, -1), (1, -1), (1, 1), (-1, 1)]
            ],
            "indices": [0, 1, 2, 0, 2, 3],
        }
    )
    disk = [((0, 0, 0), (0, 0, 1), (0, 0))] + [
        (
            (math.cos(i * math.tau / 64), math.sin(i * math.tau / 64), 0),
            (0, 0, 1),
            (0, 0),
        )
        for i in range(64)
    ]
    meshes.append(
        {
            "vertices": disk,
            "indices": [v for i in range(64) for v in (0, 1 + i, 1 + (i + 1) % 64)],
        }
    )
    items = [identity_item(i, i) for i in range(4)] + emitter_items
    if len(materials) != 8 or len(meshes) != 6 or len(items) > 256:
        raise ValueError("renderer profile capacity")
    # Camera's PBRT CTM is camera-from-world. This fixture contains a rigid camera.
    rotation = [[camera[c * 4 + r] for c in range(3)] for r in range(3)]
    translation = camera[12:15]
    if any(
        abs(dot(rotation[a], rotation[b]) - (1 if a == b else 0)) > 1e-5
        for a in range(3)
        for b in range(3)
    ):
        raise ValueError("camera must be rigid")
    eye = [-sum(rotation[r][c] * translation[r] for r in range(3)) for c in range(3)]
    forward = [rotation[2][c] for c in range(3)]
    up = [rotation[1][c] for c in range(3)]
    target = add(eye, forward)
    blob = bytearray(b"LUSCN01\0")
    blob += struct.pack(
        "<5I10f",
        1,
        len(meshes),
        len(materials),
        len(textures),
        len(items),
        *eye,
        *target,
        *up,
        math.radians(fov),
    )
    for width, height, data in textures:
        blob += struct.pack("<3I", width, height, len(data)) + data
    for texture, metallic, roughness, emission in materials:
        blob += struct.pack("<I5f", texture, metallic, roughness, *emission)
    for mesh in meshes:
        blob += struct.pack("<2I", len(mesh["vertices"]), len(mesh["indices"]))
        for p, n, uv in mesh["vertices"]:
            blob += struct.pack("<8f", *p, *n, *uv)
        blob += struct.pack("<" + "I" * len(mesh["indices"]), *mesh["indices"])
    for mesh, material, transform in items:
        blob += struct.pack("<2I12f", mesh, material, *transform)
    if len(blob) > 64 * 1024 * 1024:
        raise ValueError("bundle byte limit")
    license_text = bounded_bytes(source / "LICENSE.txt", 1024 * 1024).decode("utf-8")
    input_hashes = {
        p: hashlib.sha256(bounded_bytes(asset_path(source, p))).hexdigest()
        for p in sorted(inputs)
    }
    manifest = {
        "version": 1,
        "source": "Modern Hall / staircase2",
        "author": "NewSee2l035",
        "license": "https://creativecommons.org/licenses/by/3.0/",
        "source_format": "PBRT v4 subset + binary PLY + uncompressed TGA",
        "inputs": input_hashes,
        "bundle_sha256": hashlib.sha256(blob).hexdigest(),
        "source_ply_meshes": source_meshes,
        "source_triangles": source_triangles,
        "discarded_zero_area_triangles": degenerate_triangles,
        "meshes": len(meshes),
        "materials": len(materials),
        "items": len(items),
        "cooked_triangles": sum(len(m["indices"]) // 3 for m in meshes),
        "rendered_triangles": sum(len(meshes[i[0]]["indices"]) // 3 for i in items),
        "bytes": len(blob),
        "camera": {"eye": eye, "target": target, "up": up, "fov_degrees": fov},
        "approximations": [
            "PBRT coated diffuse mapped to frozen RGB GGX/Lambert; no layer multiple scattering",
            "conductor eta/k reduced to RGB normal-incidence F0 and Schlick",
            "diffuse surfaces use L6 dielectric F0 0.04",
            "L6 surface emission is two-sided; PBRT diffuse area lights default to one-sided",
            "single retained texture mip; no PBRT footprint/trilinear filtering",
            "disk tessellation: 64 segments",
            "emissive planar grids represented by equivalent quads",
            "PBRT integrator/sampler options replaced by explicit L6 capture settings",
        ],
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "scene.luscn").write_bytes(blob)
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "LICENSE.txt").write_text(license_text)
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(cook(args.source, args.output), indent=2))
    except (ValueError, OSError, KeyError, IndexError, struct.error) as error:
        parser.exit(1, f"conversion failed: {error}\n")
