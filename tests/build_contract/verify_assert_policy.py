"""Verify real generated headers, rejected overrides, and flavor/config pairs."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile

from verify_compile_policy import compiler_options


def main():
    build, source = map(Path, sys.argv[1:3])
    cmake = sys.argv[3]
    flavor, enabled, check_break = map(int, sys.argv[4:7])
    entries = json.loads((build / "compile_commands.json").read_text())
    entry = next(e for e in entries if e["file"].endswith("base/src/version.cpp"))
    args = compiler_options(entry) + ["-x", "c++", "-fsyntax-only", "-"]
    body = f"""#include <ludus/foundation/base/assert_config.hpp>
static_assert(LUDUS_BUILD_FLAVOR_ID == {flavor});
static_assert(LUDUS_ENABLE_ASSERTS == {enabled});
static_assert(LUDUS_BREAK_ON_CHECK == {check_break});
"""
    for ndebug in ("#define NDEBUG 1\n", "#undef NDEBUG\n"):
        subprocess.run(args, input=ndebug + body, cwd=entry["directory"], text=True, check=True)
    for name in ("LUDUS_ENABLE_ASSERTS", "LUDUS_BREAK_ON_CHECK", "LUDUS_BUILD_FLAVOR_ID", "LUDUS_ASSERT_POLICY_VERSION"):
        result = subprocess.run(args, input=f"#define {name} 0\n" + body,
                                cwd=entry["directory"], text=True, capture_output=True)
        if result.returncode == 0 or "overrides are forbidden" not in result.stderr:
            raise RuntimeError(f"Did not reject {name} override correctly: {result.stderr}")

    script = source / "cmake/EngineBuildFlavor.cmake"
    good = [("Debug", "Debug"), ("Development", "RelWithDebInfo"), ("Profile", "RelWithDebInfo"),
            ("Release", "Release"), ("Release", "MinSizeRel")]
    bad = [("", "Debug"), ("Profile", "Debug"), ("Shipping", "Release"), ("Development", "Release")]
    for build_flavor, config in good + bad:
        result = subprocess.run([cmake, f"-DLUDUS_BUILD_FLAVOR={build_flavor}",
                                 f"-DCMAKE_BUILD_TYPE={config}", "-P", str(script)],
                                text=True, capture_output=True)
        if (result.returncode == 0) != ((build_flavor, config) in good):
            raise RuntimeError(f"Wrong flavor validation: {build_flavor}/{config}: {result.stderr}")
    for override in ("-DLUDUS_ENABLE_ASSERTS=1", "-DLUDUS_BREAK_ON_CHECK=0", "-DCMAKE_CONFIGURATION_TYPES=Debug;Release"):
        result = subprocess.run([cmake, "-DLUDUS_BUILD_FLAVOR=Profile", "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
                                 override, "-P", str(script)], text=True, capture_output=True)
        if result.returncode == 0:
            raise RuntimeError(f"Configuration accepted unsupported input: {override}")

    with tempfile.TemporaryDirectory(prefix="ludus-sdk-guard-") as temp:
        prefix = Path(temp)
        guard = [cmake, f"-DCMAKE_INSTALL_PREFIX={prefix}", "-P", str(build / "cmake/CheckSdkVariant.cmake")]
        subprocess.run(guard, check=True)
        manifest = prefix / "share/Ludus/LudusSdkManifest.json"
        manifest.parent.mkdir(parents=True)
        current = (build / "cmake/LudusSdkManifest.json").read_text()
        manifest.write_text(current)
        subprocess.run(guard, check=True)
        for contents in ('{"sdk_variant":"incompatible"}', '{}'):
            manifest.write_text(contents)
            result = subprocess.run(guard, text=True, capture_output=True)
            if result.returncode == 0 or "clean, separate prefix" not in result.stderr:
                raise RuntimeError(f"Install guard did not reject variant: {result.stderr}")
    print("Verified generated assertion policy, configuration failures, and SDK variant guard")


if __name__ == "__main__":
    main()
