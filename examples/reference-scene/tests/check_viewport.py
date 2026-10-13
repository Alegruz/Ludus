"""Installed SDK raster viewport uses the supplied scene, without reference setup."""

from pathlib import Path
import subprocess
import sys
import tempfile

from check_capture import fixture


def main():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        (root / "scene.luscn").write_bytes(fixture())
        subprocess.run(
            [
                str(Path(sys.argv[1]).resolve()),
                str(root),
                str(root),
                "8",
                "2",
                "viewport",
            ],
            check=True,
            timeout=30,
        )
        pixels = (root / "viewport.rgba").read_bytes()
        # Authored .5 emission, Reinhard to linear 1/3, then sRGB; consumer preserves display values.
        assert pixels == bytes((156, 156, 156, 255)) * 64, pixels
        assert not (root / "capture.luhdr").exists()
        assert not (root / "gpu-preview.bin").exists()
    print("Installed raster viewport renders the supplied scene without tracing setup.")


if __name__ == "__main__":
    main()
