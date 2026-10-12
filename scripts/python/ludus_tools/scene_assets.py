"""Durable, content-addressed scene assets shared by installed CLI and Editor.

Loose files are the initial storage profile. Catalog publication is the commit
point: abandoned immutable files are safe, while readers never see half an import.
Level documents bind logical UUIDs; the catalog resolves exact artifact revisions.
Neither binding depends on source paths or GPU handles.
"""
from __future__ import annotations

from contextlib import contextmanager
from contextvars import ContextVar
from functools import wraps
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import shutil
import tempfile
import uuid

from .errors import ToolingError
from . import scene_cook

MAX_BYTES = 64 * 1024 * 1024
MAX_METADATA = 1024 * 1024
PROFILE = "resident-reference-v1"
IDENTITY = [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0]


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def fail(message, code="InvalidScene"):
    raise ToolingError(code, message)


def inside(root, relative):
    root = Path(root).resolve()
    relative = Path(relative)
    if relative.is_absolute() or ".." in relative.parts:
        fail("Project asset path must be relative and contained")
    path = (root / relative).resolve()
    if not path.is_relative_to(root):
        fail("Project asset symlink escapes the project")
    return path


def read(path, maximum=MAX_METADATA):
    try:
        return scene_cook.bounded_bytes(path, maximum)
    except (OSError, ValueError) as error:
        fail(str(error), "AssetCorrupt")


def _object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            fail("Duplicate JSON field: " + key, "AssetCorrupt")
        value[key] = item
    return value


def document(path):
    try:
        return json.loads(read(path), object_pairs_hook=_object)
    except (ValueError, UnicodeError, RecursionError) as error:
        fail(str(error), "AssetCorrupt")


def atomic(path, data, immutable=False, cancelled=lambda: False):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".pending-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        if cancelled():
            fail("Publication cancelled before commit", "InstallCancelled")
        if immutable:
            try:
                os.link(temporary, path)
            except FileExistsError:
                if read(path, MAX_BYTES) != data:
                    fail("Immutable artifact changed during publication", "Conflict")
        else:
            os.replace(temporary, path)
        if hasattr(os, "O_DIRECTORY"):
            directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
    finally:
        Path(temporary).unlink(missing_ok=True)


_LOCK = ContextVar("scene_asset_lock", default=None)


@contextmanager
def _lock(root, exclusive):
    identity = str(Path(root).resolve())
    active = _LOCK.get()
    if active is not None and active[0] == identity:
        if exclusive and not active[1]:
            fail("Cannot upgrade a scene reader to a writer", "Busy")
        yield
        return
    try:
        import fcntl
    except ImportError:
        fail("This initial scene host requires macOS or Linux", "MissingTools")
    path = inside(root, "assets/.scene-writer.lock")
    # Older immutable releases may have no coordination file. Readers do not
    # create files; every editable save/import and new release creates this lock.
    if not exclusive and not path.exists():
        yield
        return
    if exclusive:
        path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a" if exclusive else "r") as handle:
        try:
            fcntl.flock(handle, (fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH) | fcntl.LOCK_NB)
        except BlockingIOError:
            fail("Another scene publication is active; retry after it completes", "Busy")
        token = _LOCK.set((identity, exclusive))
        try:
            yield
        finally:
            _LOCK.reset(token)
            fcntl.flock(handle, fcntl.LOCK_UN)


@contextmanager
def writer(root):
    """Exclusive native publication lock, released by the kernel on process exit."""
    with _lock(root, True):
        yield


def _reader(function):
    @wraps(function)
    def read_locked(root, *args, **kwargs):
        with _lock(root, False):
            return function(root, *args, **kwargs)
    return read_locked


@_reader
def catalog(root):
    path = inside(root, "assets/catalog.json")
    value = document(path) if path.exists() else {"version": 1, "resources": {}}
    if not isinstance(value, dict) or set(value) != {"version", "resources"} or value["version"] != 1:
        fail("Unsupported scene catalog")
    if not isinstance(value["resources"], dict) or len(value["resources"]) > 4096:
        fail("Scene catalog resource limit")
    return value


class Reader:
    def __init__(self, data):
        self.data, self.offset = data, 0

    def take(self, count):
        if count < 0 or self.offset + count > len(self.data):
            fail("Truncated cooked scene")
        result = self.data[self.offset:self.offset + count]
        self.offset += count
        return result

    def fields(self, format):
        values = struct.unpack(format, self.take(struct.calcsize(format)))
        if any(isinstance(value, float) and not math.isfinite(value) for value in values):
            fail("Nonfinite cooked scene value")
        return values


def split_bundle(data):
    """Validate the complete binary profile before any asset publication."""
    if len(data) > MAX_BYTES:
        fail("Cooked scene byte limit")
    reader = Reader(data)
    if reader.take(8) != b"LUSCN01\0":
        fail("Unsupported cooked scene magic")
    version, meshes, materials, textures, objects, *camera = reader.fields("<5I10f")
    if version not in (1, 2) or not 1 <= meshes <= 6 or not 1 <= materials <= 8 or not 0 <= textures <= 8 or not 1 <= objects <= 128:
        fail("Cooked scene exceeds resident reference profile")
    if not 0 < camera[-1] < math.pi:
        fail("Invalid camera field of view")
    parts = {"texture": [], "material": [], "mesh": []}
    texture_bytes = 0
    for _ in range(textures):
        header = reader.take(12)
        width, height, count = struct.unpack("<3I", header)
        texture_bytes += count
        if not 0 < width <= 4096 or not 0 < height <= 4096 or count != width * height * 4 or count > 16 * 1024 * 1024 or texture_bytes > 32 * 1024 * 1024:
            fail("Cooked texture budget")
        parts["texture"].append(header + reader.take(count))
    for _ in range(materials):
        start = reader.offset
        texture, = reader.fields("<I")
        address = reader.fields("<I")[0] if version == 2 else (1 if 0 < texture < textures else 0)
        metallic, roughness, *emission = reader.fields("<5f")
        if address > 1:
            fail("Invalid cooked sampler address")
        if texture != 0xffffffff and texture >= textures or not 0 <= metallic <= 1 or not .045 <= roughness <= 1 or min(emission) < 0 or max(emission) > 10000:
            fail("Invalid cooked material")
        parts["material"].append((reader.data[start:reader.offset], texture))
    vertices = 0
    for _ in range(meshes):
        start = reader.offset
        vertex_count, index_count = reader.fields("<2I")
        vertices += vertex_count
        if not 0 < vertex_count <= 500000 or vertices > 500000 or not 0 < index_count <= 1500000 or index_count % 3:
            fail("Cooked geometry budget")
        vertex_data = reader.take(vertex_count * 32)
        for vertex in struct.iter_unpack("<8f", vertex_data):
            if not all(math.isfinite(v) for v in vertex) or sum(v * v for v in vertex[3:6]) <= 1e-12:
                fail("Invalid cooked vertex")
        for (index,) in struct.iter_unpack("<I", reader.take(index_count * 4)):
            if index >= vertex_count:
                fail("Cooked index outside mesh")
        parts["mesh"].append(reader.data[start:reader.offset])
    items = []
    for _ in range(objects):
        mesh, material, *transform = reader.fields("<2I12f")
        if mesh >= meshes or material >= materials:
            fail("Cooked object binding")
        items.append((mesh, material, transform))
    if reader.offset != len(data):
        fail("Trailing cooked scene bytes")
    return parts, items, camera


def _artifact_key(kind, content_hash, dependencies, dependency_keys, build):
    return digest(canonical({"schema": 1, "kind": kind, "content": content_hash,
        "dependencies": dependencies, "dependency_keys": dependency_keys, "build": build}))


def _put(root, resources, kind, data, dependencies, build, identity):
    content_hash = digest(data)
    dependency_keys = {child: resources[child]["key"] for child in dependencies}
    key = _artifact_key(kind, content_hash, dependencies, dependency_keys, build)
    relative = "assets/objects/" + key + ".bin"
    path = inside(root, relative)
    if path.exists():
        if digest(read(path, MAX_BYTES)) != content_hash:
            fail("An existing immutable artifact is corrupt", "AssetCorrupt")
    else:
        atomic(path, data, immutable=True)
    # A logical UUID remains stable across explicitly committed artifact revisions.
    resources[identity] = {"kind": kind, "key": key, "sha256": content_hash, "bytes": len(data),
        "path": relative, "dependencies": dependencies, "dependency_keys": dependency_keys, "build": build}
    return identity


def new_level(name="Scene"):
    return {"version": 1, "id": str(uuid.uuid4()), "name": name, "profile": PROFILE,
            "objects": [], "camera": {"eye": [0, 0, 3], "target": [0, 0, 0],
            "up": [0, 1, 0], "fov": math.pi / 3}, "imports": []}


def validate_level(value):
    if not isinstance(value, dict) or set(value) != {"version", "id", "name", "profile", "objects", "camera", "imports"}:
        fail("Invalid level fields")
    if value["version"] != 1 or value["profile"] != PROFILE or not isinstance(value["name"], str) or not 1 <= len(value["name"]) <= 128:
        fail("Unsupported level profile")
    try:
        uuid.UUID(value["id"])
    except (ValueError, TypeError, AttributeError):
        fail("Invalid persistent level ID")
    objects = value["objects"]
    if not isinstance(objects, list) or len(objects) > 128:
        fail("Level object limit")
    ids = set()
    for item in objects:
        if not isinstance(item, dict) or set(item) != {"id", "name", "parent", "mesh", "material", "transform"}:
            fail("Invalid level object fields")
        try:
            uuid.UUID(item["id"])
        except (ValueError, TypeError, AttributeError):
            fail("Invalid persistent object ID")
        if item["id"] in ids or not isinstance(item["name"], str) or not 1 <= len(item["name"]) <= 128:
            fail("Duplicate object ID or invalid name")
        ids.add(item["id"])
        if not isinstance(item["parent"], str) or not _numbers(item["transform"], 12):
            fail("Invalid object hierarchy/transform")
        if not _resource_id(item["mesh"], "mesh") or not _resource_id(item["material"], "material"):
            fail("Invalid stable resource ID")
    parents = {item["id"]: item["parent"] for item in objects}
    for identity in parents:
        seen = set()
        while identity:
            if identity not in parents or identity in seen:
                fail("Missing parent or hierarchy cycle")
            seen.add(identity)
            identity = parents[identity]
    camera = value["camera"]
    if not isinstance(camera, dict) or set(camera) != {"eye", "target", "up", "fov"}:
        fail("Invalid camera fields")
    if not all(_numbers(camera[field], 3) for field in ("eye", "target", "up")) or not _numbers([camera["fov"]], 1) or not 0 < camera["fov"] < math.pi:
        fail("Invalid camera")
    if not isinstance(value["imports"], list) or len(value["imports"]) > 128 or any(not _resource_id(i, "scene") for i in value["imports"]):
        fail("Invalid import provenance")
    forward = [camera["target"][i] - camera["eye"][i] for i in range(3)]
    up = camera["up"]
    cross = [forward[1]*up[2]-forward[2]*up[1], forward[2]*up[0]-forward[0]*up[2], forward[0]*up[1]-forward[1]*up[0]]
    if sum(v*v for v in forward) <= 1e-12 or sum(v*v for v in cross) <= 1e-12:
        fail("Degenerate authored camera")
    return value


def _numbers(values, count):
    return isinstance(values, list) and len(values) == count and all(type(v) in (int, float) and math.isfinite(v) and abs(v) <= 1e12 for v in values)


def _resource_id(value, kind):
    return isinstance(value, str) and re.fullmatch(kind + r"/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", value) is not None


@_reader
def closure(root, level, current=None):
    validate_level(level)
    resources = (current or catalog(root))["resources"]
    selected, visiting = {}, set()

    def visit(identity, depth=0):
        if identity in selected:
            return
        if identity in visiting or depth > 8:
            fail("Cooked dependency cycle or depth limit")
        if identity not in resources:
            fail("Missing cooked resource " + identity, "AssetCorrupt")
        record = resources[identity]
        if not isinstance(record, dict) or set(record) != {"kind", "key", "sha256", "bytes", "path", "dependencies", "dependency_keys", "build"}:
            fail("Invalid artifact record")
        if record["kind"] not in ("mesh", "material", "texture", "scene", "license") or not _resource_id(identity, record["kind"]) or not isinstance(record["dependencies"], list) or len(record["dependencies"]) > 32:
            fail("Invalid artifact identity/dependencies")
        data = read(inside(root, record["path"]), MAX_BYTES)
        if len(data) != record["bytes"] or digest(data) != record["sha256"]:
            fail("Cooked artifact digest mismatch: " + identity, "AssetCorrupt")
        key = _artifact_key(record["kind"], record["sha256"], record["dependencies"], record["dependency_keys"], record["build"])
        if key != record["key"]:
            fail("Cooked artifact key mismatch", "AssetCorrupt")
        visiting.add(identity)
        if not isinstance(record["dependency_keys"], dict) or set(record["dependency_keys"]) != set(record["dependencies"]):
            fail("Invalid exact dependency bindings")
        for child in record["dependencies"]:
            visit(child, depth + 1)
            if selected[child]["key"] != record["dependency_keys"][child]:
                fail("Dependency artifact revision mismatch", "Conflict")
        visiting.remove(identity)
        selected[identity] = record
        if len(selected) > 64:
            fail("Resident reference closure exceeds 64 resources")

    for item in level["objects"]:
        visit(item["mesh"])
        visit(item["material"])
    for identity in level["imports"]:
        visit(identity)
    if sum(record["bytes"] for record in selected.values()) > MAX_BYTES:
        fail("Resident closure exceeds 64 MiB")
    return dict(sorted(selected.items()))


def prepare_import(root, source, level, cancelled=lambda: False):
    """Cook on a worker. Return a complete candidate; never save the open level.

    Return (level, private catalog, expected published digest). The catalog is
    NOT published here. Renderer admission can fail/cancel without changing assets
    referenced by existing saved levels. Unreachable immutable blobs are harmless.
    """
    validate_level(level)
    if cancelled():
        fail("Scene import cancelled", "InstallCancelled")
    with tempfile.TemporaryDirectory(prefix="ludus-scene-") as directory:
        output = Path(directory)
        try:
            with scene_cook.cancellation(cancelled):
                manifest = scene_cook.cook(Path(source), output)
            parts, items, camera = split_bundle(read(output / "scene.luscn", MAX_BYTES))
        except scene_cook.CookCancelled as error:
            fail(str(error), "InstallCancelled")
        except scene_cook.SourceConflict as error:
            fail(str(error), "Conflict")
        except (ValueError, OSError, KeyError, IndexError, struct.error) as error:
            fail(str(error), "UnsupportedScene")
        # Source read consistency is checked before the catalog commit point.
        for name, expected in manifest["inputs"].items():
            if digest(read(scene_cook.asset_path(Path(source), name), MAX_BYTES)) != expected:
                fail("Source changed during conversion", "Conflict")
        build = {"importer": "pbrt-reference-v1", "importer_sha256": digest(read(Path(scene_cook.__file__), MAX_METADATA)),
                 "publisher_sha256": digest(read(Path(__file__), MAX_METADATA)),
                 "source_inputs": manifest["inputs"], "settings": {"profile": PROFILE},
                 "schema": 1, "target": "portable-rgba8-f32-le"}
        candidate = json.loads(canonical(level))
        with writer(root):
            current = catalog(root)
            catalog_path = inside(root, "assets/catalog.json")
            expected_catalog = digest(read(catalog_path)) if catalog_path.exists() else None
            resources = current["resources"]
            namespace = uuid.uuid4()
            origin = str(Path(source).resolve())
            for identity, record in resources.items():
                if record["kind"] == "scene" and _resource_id(identity, "scene"):
                    previous = document(inside(root, record["path"]))
                    if previous.get("source_origin") == origin:
                        namespace = uuid.UUID(identity.split("/", 1)[1])
                        break
            def logical(kind, part):
                return kind + "/" + str(uuid.uuid5(namespace, kind + "/" + str(part)))
            textures = [_put(root, resources, "texture", data, [], build, logical("texture", i))
                        for i, data in enumerate(parts["texture"])]
            materials = []
            for i, (data, texture) in enumerate(parts["material"]):
                # Serialized material uses the stable texture identity, never a local slot.
                _, metallic, roughness, *emission = struct.unpack("<I5f", data)
                description = {"texture": textures[texture] if texture != 0xffffffff else "",
                               "metallic": metallic, "roughness": roughness, "emission": emission,
                               "address": "repeat" if 0 < texture < len(textures) else "clamp"}
                materials.append(_put(root, resources, "material", canonical(description),
                                      [textures[texture]] if texture != 0xffffffff else [], build, logical("material", i)))
            meshes = [_put(root, resources, "mesh", data, [], build, logical("mesh", i))
                      for i, data in enumerate(parts["mesh"])]
            license_id = _put(root, resources, "license", read(output / "LICENSE.txt"), [], build, logical("license", 0))
            manifest["source_origin"] = origin  # Optional provenance; never read on reopen.
            manifest["template"] = {"objects": [
                {"mesh": meshes[m], "material": materials[a], "transform": t} for m, a, t in items],
                "camera": {"eye": camera[:3], "target": camera[3:6], "up": camera[6:9], "fov": camera[9]}}
            scene_id = _put(root, resources, "scene", canonical(manifest),
                sorted(set(meshes + materials + [license_id])), build, "scene/" + str(namespace))
            for i, (mesh, material, transform) in enumerate(items):
                candidate["objects"].append({"id": str(uuid.uuid4()), "name": f"Imported object {i + 1}",
                    "parent": "", "mesh": meshes[mesh], "material": materials[material], "transform": transform})
            candidate["camera"] = {"eye": camera[:3], "target": camera[3:6], "up": camera[6:9], "fov": camera[9]}
            if scene_id not in candidate["imports"]:
                candidate["imports"].append(scene_id)
            closure(root, candidate, current)
            # Validate render admission as well as durable byte integrity.
            assemble(root, candidate, current)
            if cancelled():
                fail("Scene import cancelled", "InstallCancelled")
            if len(resources) > 4096:
                fail("Project catalog resource limit")
            catalog_bytes = canonical(current)
            if len(catalog_bytes) > MAX_METADATA:
                fail("Project catalog metadata exceeds 1 MiB")
            for name, expected in manifest["inputs"].items():
                if digest(read(scene_cook.asset_path(Path(source), name), MAX_BYTES)) != expected:
                    fail("Source changed before catalog publication", "Conflict")
            if cancelled():
                fail("Scene import cancelled", "InstallCancelled")
        return candidate, current, expected_catalog


def _validate_import(root, level, candidate_catalog, expected, source, cancelled):
    """Validate under an already held writer lock, without publishing anything."""
    path = inside(root, "assets/catalog.json")
    actual = digest(read(path)) if path.exists() else None
    if actual != expected:
        fail("Project asset catalog changed while preparing the candidate", "Conflict")
    closure(root, level, candidate_catalog)
    assemble(root, level, candidate_catalog)
    origin = str(Path(source).resolve())
    for identity in level["imports"]:
        record = candidate_catalog["resources"][identity]
        manifest = document(inside(root, record["path"]))
        if manifest.get("source_origin") == origin:
            for name, expected_input in manifest["inputs"].items():
                if digest(read(scene_cook.asset_path(Path(source), name), MAX_BYTES)) != expected_input:
                    fail("Source changed before catalog publication", "Conflict")
    data = canonical(candidate_catalog)
    if len(data) > MAX_METADATA:
        fail("Project catalog metadata exceeds 1 MiB")
    if cancelled():
        fail("Scene import cancelled", "InstallCancelled")
    return data


def commit_import(root, level, candidate_catalog, expected, source, cancelled=lambda: False):
    """Commit a prevalidated candidate after renderer admission, with compare-and-publish."""
    with writer(root):
        data = _validate_import(root, level, candidate_catalog, expected, source, cancelled)
        atomic(inside(root, "assets/catalog.json"), data, cancelled=cancelled)


def _sync_directory(path):
    if hasattr(os, "O_DIRECTORY"):
        descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)


def import_saved_level(root, relative, source):
    """CLI pair publication with staged files, exclusive readers and rollback.

    All files are synced before replacement. Failed replacement/synchronization
    restores catalog and level bytes; a failed rollback retains owned backups and
    reports RecoveryRequired. This is an operation-error rollback boundary, not a
    two-file crash-atomic filesystem transaction.
    """
    path = inside(root, relative)
    if path.suffix != ".lulevel":
        fail("Levels use the .lulevel extension")
    with _lock(root, False):
        previous = read(path)
        level = open_level(root, relative)
        if read(path) != previous:
            fail("Saved level changed while reading", "Conflict")
    candidate, current, expected = prepare_import(root, source, level)
    with writer(root):
        data = _validate_import(root, candidate, current, expected, source, lambda: False)
        if read(path) != previous:
            fail("Saved level changed during import", "Conflict")
        level_data = canonical(candidate)
        if len(level_data) > MAX_METADATA:
            fail("Level document byte limit")
        catalog_path = inside(root, "assets/catalog.json")
        old_catalog = read(catalog_path) if catalog_path.exists() else None
        catalog_stage = Path(tempfile.mkdtemp(prefix=".scene-import-", dir=catalog_path.parent))
        level_stage = None
        retain = False
        catalog_replaced = level_replaced = False
        try:
            # Each stage is on its destination filesystem; an unwritable level
            # directory fails before the published catalog is changed.
            level_stage = Path(tempfile.mkdtemp(prefix=".scene-import-", dir=path.parent))
            atomic(catalog_stage / "next-catalog", data)
            if old_catalog is not None:
                atomic(catalog_stage / "previous-catalog", old_catalog)
            atomic(level_stage / "next-level", level_data)
            atomic(level_stage / "previous-level", previous)
            try:
                if read(path) != previous:
                    fail("Saved level changed before import publication", "Conflict")
                os.replace(catalog_stage / "next-catalog", catalog_path)
                catalog_replaced = True
                if read(path) != previous:
                    fail("Saved level changed before import publication", "Conflict")
                os.replace(level_stage / "next-level", path)
                level_replaced = True
                _sync_directory(catalog_path.parent)
                _sync_directory(path.parent)
            except (OSError, ToolingError):
                try:
                    if level_replaced:
                        os.replace(level_stage / "previous-level", path)
                    if catalog_replaced:
                        if old_catalog is None:
                            catalog_path.unlink()
                        else:
                            os.replace(catalog_stage / "previous-catalog", catalog_path)
                    _sync_directory(catalog_path.parent)
                    _sync_directory(path.parent)
                except OSError as error:
                    retain = True
                    fail(f"Import rollback failed; recovery files retained in {catalog_stage} and {level_stage}: {error}", "RecoveryRequired")
                raise
        except OSError as error:
            fail(str(error), "StorageFailure")
        finally:
            if not retain:
                shutil.rmtree(catalog_stage)
                if level_stage is not None:
                    shutil.rmtree(level_stage)
    return candidate


def import_scene(root, source, level, cancelled=lambda: False):
    """CLI composition: prepare and explicitly commit; never save the caller's level."""
    candidate, current, expected = prepare_import(root, source, level, cancelled)
    commit_import(root, candidate, current, expected, source, cancelled)
    return candidate


def save_level(root, relative, level, expected=None):
    """Validate durable closure and compare disk bytes before atomic level replacement."""
    path = inside(root, relative)
    if path.suffix != ".lulevel":
        fail("Levels use the .lulevel extension")
    with writer(root):
        closure(root, level)
        assemble(root, level)
        previous = read(path) if path.exists() else None
        if previous != expected:
            fail("Saved level changed on disk", "Conflict")
        data = canonical(level)
        if len(data) > MAX_METADATA:
            fail("Level document byte limit")
        atomic(path, data)
    return data


@_reader
def open_level(root, relative):
    level = validate_level(document(inside(root, relative)))
    closure(root, level)
    assemble(root, level)
    return level


@_reader
def assemble(root, level, current=None):
    """Runtime artifact decoding only; this function never calls the source importer.

    The binary is an interchange buffer for the initial renderer admission profile;
    persistent level bindings continue to use mesh/material/texture resource IDs.
    """
    resources = closure(root, level, current)
    meshes = sorted({o["mesh"] for o in level["objects"]})
    materials = sorted({o["material"] for o in level["objects"]})
    if not meshes:
        return b""
    descriptions = {i: document(inside(root, resources[i]["path"])) for i in materials}
    for identity, description in descriptions.items():
        if not isinstance(description, dict) or set(description) != {"texture", "metallic", "roughness", "emission", "address"}:
            fail("Invalid cooked material fields")
        if description["address"] not in ("clamp", "repeat"):
            fail("Invalid cooked sampler address")
        texture = description["texture"]
        if not isinstance(texture, str) or texture and not _resource_id(texture, "texture") or resources[identity]["dependencies"] != ([texture] if texture else []):
            fail("Cooked material dependency mismatch")
        if not _numbers([description["metallic"], description["roughness"]], 2) or not 0 <= description["metallic"] <= 1 or not .045 <= description["roughness"] <= 1 or not _numbers(description["emission"], 3) or min(description["emission"]) < 0 or max(description["emission"]) > 10000:
            fail("Invalid cooked material values")
    textures = sorted({d["texture"] for d in descriptions.values() if d["texture"]})
    if len(meshes) > 6 or len(materials) > 8 or len(textures) > 8:
        fail("Scene exceeds six meshes/eight materials/eight textures; no partial viewport publication")
    camera = level["camera"]
    data = bytearray(b"LUSCN01\0" + struct.pack("<5I10f", 2, len(meshes), len(materials), len(textures),
        len(level["objects"]), *camera["eye"], *camera["target"], *camera["up"], camera["fov"]))
    for identity in textures:
        data += read(inside(root, resources[identity]["path"]), MAX_BYTES)
    for identity in materials:
        value = descriptions[identity]
        data += struct.pack("<2I5f", textures.index(value["texture"]) if value["texture"] else 0xffffffff,
                            int(value["address"] == "repeat"), value["metallic"], value["roughness"], *value["emission"])
    for identity in meshes:
        data += read(inside(root, resources[identity]["path"]), MAX_BYTES)
    objects = {o["id"]: o for o in level["objects"]}
    def world(item):
        local = item["transform"]
        if not item["parent"]:
            return local
        parent = world(objects[item["parent"]])
        basis = [sum(parent[k * 3 + row] * local[col * 3 + k] for k in range(3))
                 for col in range(3) for row in range(3)]
        position = [parent[9 + row] + sum(parent[k * 3 + row] * local[9 + k] for k in range(3)) for row in range(3)]
        return basis + position
    for item in level["objects"]:
        transform = world(item)
        if not all(math.isfinite(v) and abs(v) <= 3e38 for v in transform):
            fail("World transform outside float32 range")
        data += struct.pack("<2I12f", meshes.index(item["mesh"]), materials.index(item["material"]), *transform)
    split_bundle(data)
    return bytes(data)


@_reader
def dump(root, level, current=None):
    resources = closure(root, level, current)
    return {"version": 1, "profile": PROFILE, "level": level,
            "resources": resources, "resident_bytes": sum(r["bytes"] for r in resources.values())}


@_reader
def publish(root, level, destination):
    """Publish a deterministic loose-file closure to a new directory, with licenses."""
    destination = Path(destination)
    if destination.exists():
        fail("Release destination already exists", "DestinationExists")
    resources = closure(root, level)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".scene-release-", dir=destination.parent) as directory:
        stage = Path(directory) / "project"
        stage.mkdir()
        for identity, record in resources.items():
            atomic(inside(stage, record["path"]), read(inside(root, record["path"]), MAX_BYTES))
        atomic(inside(stage, "assets/catalog.json"), canonical({"version": 1, "resources": resources}))
        atomic(inside(stage, "assets/.scene-writer.lock"), b"")
        atomic(inside(stage, "scenes/main.lulevel"), canonical(level))
        atomic(inside(stage, "scene.luscn"), assemble(root, level))
        atomic(inside(stage, "scene-release.json"), canonical(dump(stage, level)))
        os.rename(stage, destination)
    return dump(destination, level)
