"""Installed-SDK full prepared-scene acceptance with independent serialized input."""

import argparse
from pathlib import Path
import struct
import tempfile

import acceptance
from check_capture import fixture


def complete_fixture():
    data = fixture()
    struct.pack_into("<I", data, 24, 128)
    del data[-56:]
    # All six meshes/eight materials are referenced. The last object is the only
    # camera-visible emitter; dropping the tail gives a black image. Offscreen
    # objects must still enter the source-order path and reference ray scene.
    for index in range(128):
        data += struct.pack("<2I12f", index % 6, index % 8, 1, 0, 0, 0, 1, 0, 0, 0, 1,
                            0 if index == 127 else 20, 0, 0)
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--native", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        (root / "scene.luscn").write_bytes(complete_fixture())
        report = acceptance.run(args.binary, root, root / "evidence", 16, 2, args.native)
        assert report["counts"] == {"textures": 3, "materials": 8, "meshes": 6, "objects": 128,
                                    "vertices": 24, "stored_triangles": 12, "instanced_triangles": 256}
        assert report["runs"]["cpu"]["visible"] == 1
        assert report["runs"]["cpu"]["culled"] == 127
        assert report["runs"]["cpu"]["observed_source_ids"] == [128]
        raster = (root / "evidence/cpu/raster-reference.rgba").read_bytes()
        assert all(127 <= value <= 128 if index % 4 != 3 else value == 255
                   for index, value in enumerate(raster)), raster
        _, pixels = acceptance.hdr_decoder()((root / "evidence/cpu/capture.luhdr").read_bytes())
        assert all(pixel[:3] == (.5, .5, .5) for pixel in pixels), pixels
        assert (root / "evidence/acceptance.json").is_file()
    print("Complete 128-object submission, tail-emitter pixels, detached capture and requested traversal parity passed.")


if __name__ == "__main__":
    main()
