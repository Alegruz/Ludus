"""Select a supported Python before importing setup or creating its GUI.

Keep this bootstrap readable by macOS's bundled Python 3.9. Actual setup uses
the minimum version recorded in tool_versions.json.
"""
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys


def interpreter_candidates(root):
    candidates = []
    if platform.system() == "Darwin":
        # Reuse the interpreter prepared by init rather than Apple's old Tk.
        candidates.append(str(root / "out/host-tools/venv/bin/python"))
    candidates.append(sys.executable)
    versioned = []
    for directory in os.get_exec_path():
        try:
            for path in Path(directory).iterdir():
                match = re.fullmatch(r"python(\d+)\.(\d+)", path.name)
                if match and os.access(path, os.X_OK) and path.is_file():
                    versioned.append((tuple(map(int, match.groups())), str(path)))
        except OSError:
            continue
    candidates.extend(path for _, path in sorted(versioned, reverse=True))
    return list(dict.fromkeys(candidates))


def select_interpreter(root):
    versions = json.loads((root / "config/tool_versions.json").read_text(encoding="utf-8"))
    minimum = tuple(map(int, versions["minimum"]["python"].split(".")))
    for candidate in interpreter_candidates(root):
        if not Path(candidate).is_file():
            continue
        try:
            result = subprocess.run(
                [candidate, "-I", "-c", "import json,sys; print(json.dumps(list(sys.version_info[:3])))"],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
            if result.returncode == 0 and tuple(json.loads(result.stdout)) >= minimum:
                return candidate
        except (OSError, ValueError, subprocess.TimeoutExpired):
            continue
    required = versions["minimum"]["python"]
    raise RuntimeError(
        "Ludus setup requires Python " + required + " or later. Install a supported Python "
        "and add it to PATH, then rerun ./init.sh. On macOS, use Homebrew or python.org.")


def main():
    shell_sdkroot = os.environ.pop("LUDUS_INIT_SHELL_SDKROOT", None)
    if shell_sdkroot is not None:
        if shell_sdkroot:
            os.environ["SDKROOT"] = shell_sdkroot
        else:
            os.environ.pop("SDKROOT", None)
    root = Path(__file__).resolve().parents[2]
    try:
        interpreter = select_interpreter(root)
    except (OSError, ValueError, RuntimeError) as error:
        print("Ludus setup: " + str(error), file=sys.stderr)
        return 1
    os.execv(interpreter, [interpreter, str(root / "scripts/python/engine.py"), "init", *sys.argv[1:]])


if __name__ == "__main__":
    sys.exit(main())
