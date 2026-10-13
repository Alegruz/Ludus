"""Verify complete prepared-scene submission, raster parity and reference captures.

This uses the existing LUSCN01 profile; it does not import source scenes or infer
missing material support. A successful report identifies exact input/output bytes.
"""

import argparse
from contextlib import contextmanager
import hashlib
import importlib.machinery
import importlib.util
import json
import math
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import tempfile


def hdr_decoder():
    path = Path(__file__).resolve().parents[3] / "scripts/reference-hdr"
    loader = importlib.machinery.SourceFileLoader("acceptance_hdr", str(path))
    module = importlib.util.module_from_spec(importlib.util.spec_from_loader(loader.name, loader))
    loader.exec_module(module)
    return module.decode


def digest(data):
    return hashlib.sha256(data).hexdigest()


@contextmanager
def attempt_directory(output):
    attempt = Path(tempfile.mkdtemp(prefix="attempt-", dir=output))
    try:
        yield attempt
    except (ValueError, OSError, subprocess.SubprocessError, struct.error) as error:
        (attempt / "failure.txt").write_text(str(error) + "\n")
        raise
    else:
        shutil.rmtree(attempt)


def bundle_counts(data):
    """Independently walk serialized records, without the production decoder/cooker."""
    if len(data) < 68 or len(data) > 64 * 1024 * 1024 or data[:8] != b"LUSCN01\0":
        raise ValueError("invalid bounded scene bundle")
    version, meshes, materials, textures, objects = struct.unpack_from("<5I", data, 8)
    if version not in (1, 2) or not 1 <= meshes <= 6 or not 1 <= materials <= 8 or not 0 <= textures <= 8 or not 1 <= objects <= 128:
        raise ValueError("unsupported scene profile/counts")
    offset = 68

    def take(size):
        nonlocal offset
        if size < 0 or offset + size > len(data):
            raise ValueError("truncated scene bundle")
        result = memoryview(data)[offset:offset + size]
        offset += size
        return result

    for _ in range(textures):
        width, height, size = struct.unpack("<3I", take(12))
        if not width or not height or size != width * height * 4:
            raise ValueError("invalid texture extent")
        take(size)
    take(materials * (24 if version == 1 else 28))
    vertices = triangles = 0
    mesh_triangles = []
    for _ in range(meshes):
        count, indices = struct.unpack("<2I", take(8))
        if not count or not indices or indices % 3:
            raise ValueError("invalid triangle mesh")
        take(count * 32 + indices * 4)
        vertices += count
        triangles += indices // 3
        mesh_triangles.append(indices // 3)
    instanced = 0
    for _ in range(objects):
        mesh, material = struct.unpack_from("<2I", take(56))
        if mesh >= meshes or material >= materials:
            raise ValueError("invalid object binding")
        instanced += mesh_triangles[mesh]
    if offset != len(data):
        raise ValueError("trailing scene bundle bytes")
    return [textures, materials, meshes, objects, vertices, triangles, instanced]


def submission_evidence(data, expected):
    if len(data) < 120 or data[:8] != b"LUSAC01\0":
        raise ValueError("missing/invalid renderer submission evidence")
    values = struct.unpack_from("<14Q", data, 8)
    if list(values[:7]) != expected:
        raise ValueError("renderer omitted scene resources or geometry")
    objects = expected[3]
    submitted, draws, optimized_submitted, visible, culled, optimized_draws, backend = values[7:]
    if backend not in (0, 3):
        raise ValueError("unsupported native acceptance backend")
    if submitted != objects or draws != objects or optimized_submitted != objects or visible + culled != objects or not 0 < optimized_draws <= visible:
        raise ValueError("incomplete renderer submission/visibility accounting")
    if len(data) != 120 + (objects + visible) * 8:
        raise ValueError("truncated/trailing source-identity evidence")
    identities = list(struct.unpack_from(f"<{objects + visible}Q", data, 120))
    if identities[:objects] != list(range(1, objects + 1)):
        raise ValueError("reference path omitted/reordered source identities")
    optimized = identities[objects:]
    if len(set(optimized)) != visible or any(not 1 <= value <= objects for value in optimized):
        raise ValueError("duplicate/foreign optimized source identity")
    return {"backend": "vulkan" if backend == 0 else "metal", "submitted": submitted,
            "visible": visible, "culled": culled, "draws": optimized_draws,
            "reference_draws": draws, "source_ids": optimized}


def raster_pair(root, extent, prefix):
    reference = (root / f"{prefix}-reference.rgba").read_bytes()
    raster = (root / f"{prefix}-optimized.rgba").read_bytes()
    if len(reference) != extent * extent * 4 or len(raster) != len(reference):
        raise ValueError("incomplete raster readback")
    maximum = max(abs(a - b) for a, b in zip(reference, raster))
    if maximum > 1:
        raise ValueError(f"optimized raster differs from source-order pixels: {maximum}/255")
    if not any(reference[i] for i in range(len(reference)) if i % 4 != 3):
        raise ValueError("empty raster framing; no lit/emissive surface observed")
    return maximum, reference


def identity_color(source_id):
    return source_id, (source_id * 73) % 255 + 1, (source_id * 151) % 255 + 1


def verify_run(root, expected, extent, samples, native=False):
    submission = submission_evidence((root / "scene-acceptance.bin").read_bytes(), expected)
    identity = submission_evidence((root / "identity-acceptance.bin").read_bytes(), expected)
    if any(submission[key] != identity[key] for key in ("backend", "submitted", "visible", "culled")) or set(submission["source_ids"]) != set(identity["source_ids"]):
        raise ValueError("identity and lit views disagree on submitted/visible geometry")
    maximum, _ = raster_pair(root, extent, "raster")
    identity_maximum, identity_pixels = raster_pair(root, extent, "identity")
    palette = {}
    for source_id in submission["source_ids"]:
        red, green, blue = identity_color(source_id)
        for r in range(max(0, red - 1), min(255, red + 1) + 1):
            for g in range(max(0, green - 1), min(255, green + 1) + 1):
                for b in range(max(0, blue - 1), min(255, blue + 1) + 1):
                    palette[r, g, b] = source_id
    observed = set()
    for offset in range(0, len(identity_pixels), 4):
        color = tuple(identity_pixels[offset:offset + 3])
        if color == (0, 0, 0):
            continue
        if color not in palette or identity_pixels[offset + 3] != 255:
            raise ValueError("foreign/unsubmitted source identity in diagnostic pixels")
        observed.add(palette[color])
    capture = (root / "capture.luhdr").read_bytes()
    adapter = (root / "adapter-name.txt").read_bytes()
    if len(adapter) > 255 or b"\0" in adapter:
        raise ValueError("invalid bounded adapter identity")
    metadata, pixels = hdr_decoder()(capture)
    if metadata["traversal"] != ("native_queries" if native else "cpu_oracle") or metadata["partial"] or metadata["width"] != extent or metadata["height"] != extent or metadata["minimum_samples"] != samples or metadata["maximum_samples"] != samples or metadata["target_samples"] != samples:
        raise ValueError("wrong traversal, extent or incomplete reference samples")
    if not any(any(channel > 0 for channel in pixel[:3]) for pixel in pixels):
        raise ValueError("black reference capture; framing/lighting not established")
    return {
        "capture": metadata,
        **submission,
        "adapter_name": adapter.decode("utf-8") or None,
        "raster_max_byte_error": maximum,
        "identity_max_byte_error": identity_maximum,
        "observed_source_ids": sorted(observed),
        "artifact_sha256": {name: digest((root / name).read_bytes()) for name in
                            ("scene-acceptance.bin", "raster-reference.rgba", "raster-optimized.rgba", "capture.luhdr", "adapter-name.txt",
                             "identity-acceptance.bin", "identity-reference.rgba", "identity-optimized.rgba")},
    }, pixels


def native_metrics(cpu, native):
    for key in ("profile", "sampler", "revision", "scene_fingerprint", "width", "height", "seed", "max_depth", "roulette_depth", "target_samples", "world_to_clip", "camera_position", "environment", "texture_mip", "light_count", "secondary_distance"):
        if cpu[0]["capture"][key] != native[0]["capture"][key]:
            raise ValueError(f"CPU/native capture contract mismatch: {key}")
    errors = []
    failing = []
    for index, (a, b) in enumerate(zip(cpu[1], native[1])):
        error = math.sqrt(sum((x - y) ** 2 for x, y in zip(a[:3], b[:3])))
        magnitude = max(math.sqrt(sum(x * x for x in a[:3])), math.sqrt(sum(x * x for x in b[:3])))
        if error > .002 + .0001 * magnitude:
            failing.append(index)
        errors.append(error)
    return {"passed": not failing, "failing_pixels": len(failing),
            "largest_errors": [{"pixel": i, "rgb_vector_error": errors[i]} for i in
                               sorted(range(len(errors)), key=errors.__getitem__, reverse=True)[:8]],
            "max_rgb_vector_error": max(errors), "rms_rgb_vector_error": math.sqrt(sum(x * x for x in errors) / len(errors)),
            "absolute_tolerance": .002, "relative_tolerance": .0001}


def compare_native(cpu, native):
    metrics = native_metrics(cpu, native)
    if not metrics["passed"]:
        raise ValueError(f"CPU/native radiance error exceeds declared tolerance: {metrics['failing_pixels']} pixels; max={metrics['max_rgb_vector_error']}")
    return metrics


def run(binary, bundle, output, extent=32, samples=8, native=False):
    binary, bundle, output = Path(binary).resolve(), Path(bundle).resolve(), Path(output).resolve()
    if not 1 <= extent <= 1024 or not 1 <= samples <= 4096:
        raise ValueError("invalid acceptance extent/sample count")
    data = (bundle / "scene.luscn").read_bytes()
    expected = bundle_counts(data)
    manifest = json.loads((bundle / "manifest.json").read_text()) if (bundle / "manifest.json").exists() else None
    if manifest:
        for key, value in (("bundle_sha256", digest(data)), ("meshes", expected[2]), ("materials", expected[1]),
                           ("items", expected[3]), ("cooked_triangles", expected[5]), ("rendered_triangles", expected[6]), ("bytes", len(data))):
            if manifest.get(key) != value:
                raise ValueError(f"source/cooked manifest mismatch: {key}")
    binary_sha256 = digest(binary.read_bytes())
    output.mkdir(parents=True, exist_ok=True)
    # Fresh isolated attempts prevent stale success artifacts from satisfying a failed run.
    with attempt_directory(output) as attempt:
        modes = ("cpu", "native") if native else ("cpu",)
        runs = {}
        for mode in modes:
            destination = attempt / mode
            destination.mkdir()
            subprocess.run([str(binary), str(bundle), str(destination), str(extent), str(samples),
                            "native" if mode == "native" else "acceptance"], check=True, timeout=1830)
            runs[mode] = verify_run(destination, expected, extent, samples, mode == "native")
            (destination / "run.json").write_text(json.dumps({"bundle_sha256": digest(data), "binary_sha256": binary_sha256,
                                                               "counts": expected, **runs[mode][0]}, indent=2) + "\n")
        if (bundle / "scene.luscn").read_bytes() != data:
            raise ValueError("scene bundle changed during acceptance")
        if digest(binary.read_bytes()) != binary_sha256:
            raise ValueError("renderer binary changed during acceptance")
        if native:
            metrics = native_metrics(runs["cpu"], runs["native"])
            (attempt / "native-parity.json").write_text(json.dumps(metrics, indent=2) + "\n")
        report = {"version": 1, "scope": "complete prepared LUSCN01 RGB reflection profile; not source-renderer fidelity or performance qualification",
                  "host": {"system": platform.system(), "release": platform.release(), "architecture": platform.machine(), "python": platform.python_version()},
                  "bundle_sha256": digest(data), "binary_sha256": binary_sha256,
                  "counts": dict(zip(("textures", "materials", "meshes", "objects", "vertices", "stored_triangles", "instanced_triangles"), expected)),
                  "source_manifest": manifest, "runs": {mode: result[0] for mode, result in runs.items()},
                  "native_parity": compare_native(runs["cpu"], runs["native"]) if native else {"status": "not_requested"}}
        # Publish evidence only after every requested path succeeds. Individual files
        # may be replaced during publication; acceptance.json is the final commit marker.
        for mode in modes:
            destination = output / mode
            destination.mkdir(exist_ok=True)
            for artifact in (attempt / mode).iterdir():
                artifact.replace(destination / artifact.name)
        pending = attempt / "acceptance.json"
        pending.write_text(json.dumps(report, indent=2) + "\n")
        pending.replace(output / "acceptance.json")
        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--extent", type=int, default=32)
    parser.add_argument("--samples", type=int, default=8)
    parser.add_argument("--native", action="store_true", help="Require native queries and compare with CPU; never falls back")
    args = parser.parse_args()
    try:
        report = run(args.binary, args.bundle, args.output, args.extent, args.samples, args.native)
    except (ValueError, OSError, subprocess.SubprocessError, struct.error) as error:
        parser.exit(1, f"scene acceptance failed: {error}\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
