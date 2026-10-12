"""One bounded editor scene operation, using the same installed CLI backend.

The input/output locations are allocated by the trusted Editor, never by a project
script. Stdout is a bounded structured result; binary interchange goes to its job
scratch file. Only import invokes the source converter.
"""
from pathlib import Path
import json
import sys
import signal

from . import scene_assets as assets
from .errors import ToolingError


def operate(request, output, cancelled=lambda: False):
    if request.get("schema") != 1 or set(request) != {"schema", "operation", "project", "path", "source", "level", "expected"}:
        assets.fail("Unsupported editor scene protocol", "BootstrapStale")
    root = Path(request["project"])
    action = request["operation"]
    current = None
    expected_catalog = None
    committed = False
    disk_bytes = None
    if action == "open":
        disk_bytes = assets.read(assets.inside(root, request["path"]))
        value = assets.open_level(root, request["path"])
    else:
        value = assets.validate_level(request["level"])
        if action == "import":
            value, current, expected_catalog = assets.prepare_import(root, Path(request["source"]), value, cancelled)
            assets.atomic(output.with_name("catalog.json"), assets.canonical(current))
        elif action == "commit-import":
            current = assets.document(output.with_name("catalog.json"))
            assets.commit_import(root, value, current, request["expected"], Path(request["source"]), cancelled)
            committed = True
        elif action == "save":
            path = assets.inside(root, request["path"])
            previous = assets.read(path) if path.exists() else None
            if (assets.digest(previous) if previous is not None else None) != request["expected"]:
                assets.fail("Saved level changed on disk", "Conflict")
            disk_bytes = assets.save_level(root, request["path"], value, previous)
        elif action != "assemble":
            assets.fail("Unsupported editor scene operation")
    if not committed:
        assets.atomic(output, assets.assemble(root, value, current))
    closure = assets.closure(root, value, current)
    result = {"schema": 1, "ok": True, "level": value, "committed": committed,
              "artifacts": {identity: record["key"] for identity, record in closure.items()},
              "catalog_expected": expected_catalog,
              "resident_bytes": sum(record["bytes"] for record in closure.values())}
    warnings = []
    resources = (current or assets.catalog(root))["resources"]
    for identity in value["imports"]:
        manifest = assets.document(assets.inside(root, resources[identity]["path"]))
        warnings.extend(manifest.get("approximations", []))
    result["warnings"] = sorted(set(warnings))
    if action in ("open", "save"):
        if assets.read(assets.inside(root, request["path"])) != disk_bytes:
            assets.fail("Saved level changed while preparing the response", "Conflict")
        result["disk"] = assets.digest(disk_bytes)
    return result


def main():
    cancellation = [False]
    def cancel(_signal, _frame):
        cancellation[0] = True
    signal.signal(signal.SIGTERM, cancel)
    signal.signal(signal.SIGINT, cancel)
    try:
        if len(sys.argv) != 3:
            assets.fail("Editor scene operation requires owned input and output paths")
        request = assets.document(Path(sys.argv[1]))
        result = operate(request, Path(sys.argv[2]), lambda: cancellation[0])
    except ToolingError as error:
        result = {"schema": 1, "ok": False, "code": error.code, "message": error.message}
    except (OSError, ValueError, TypeError, KeyError, OverflowError) as error:
        result = {"schema": 1, "ok": False, "code": "InvalidScene", "message": str(error)}
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
