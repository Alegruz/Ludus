"""Installed public SDK exercises GPU presentation on an independently encoded scene."""

from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from check_capture import fixture


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        (root / "scene.luscn").write_bytes(fixture())
        subprocess.run(
            [binary, str(root), str(root), "8", "2", "gpu"], check=True, timeout=30
        )
        width, height, samples, reserved, low, high = struct.unpack(
            "<4I2Q", (root / "gpu-preview.bin").read_bytes()
        )
        assert (width, height, samples, reserved) == (8, 8, 2, 0)
        assert low or high
        pixels = (root / "gpu-preview.rgba").read_bytes()
        # Source-only emitter radiance .5: Reinhard/sRGB display 156/255.
        # No environment or analytic lights are supplied to this fixture.
        assert pixels == bytes((156, 156, 156, 255)) * 64, pixels
        assert not (root / "capture.luhdr").exists()
    print(
        "Installed GPU-only preview, authored emission, texture export and completion passed."
    )


if __name__ == "__main__":
    main()
