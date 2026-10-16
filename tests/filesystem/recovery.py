"""Bounded F5 crash tests; process death and block replay are distinct models.

Thanks to Pillai et al., OSDI 2014, sections 2-3, and Mohan et al., OSDI 2018,
sections 4-5, for persistence-specific oracles and bounded workload exploration.
https://www.usenix.org/conference/osdi14/technical-sessions/presentation/pillai
https://www.usenix.org/conference/osdi18/presentation/mohan
This is an independent harness, not ALICE/CrashMonkey or a port of their code.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import selectors
import signal
import subprocess
import tempfile

POLICIES = ("publish", "file", "directory")
SHAPES = ("empty", "multi")
OLD = b"durably seeded old revision\n"


def payload(shape):
    return bytes((index * 37 + index // 4096) % 251 for index in range(3 * 4096 + 17)) if shape == "multi" else b""


def boundaries(policy, shape):
    result = ["temporary-created"]
    if shape == "multi":
        result.extend(f"write-{index}" for index in range(1, 5))
    if policy != "publish":
        result.append("file-synced")
    result.extend(("temporary-closed", "replaced"))
    if policy == "directory":
        result.append("directory-synced")
    return result + ["complete"]


def seed(root, replace):
    (root / "nested").mkdir()
    if replace:
        with (root / "nested/data").open("xb") as stream:
            stream.write(OLD)
            stream.flush()
            os.fsync(stream.fileno())
    for path in (root / "nested", root):
        descriptor = os.open(path, os.O_RDONLY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)


def observe(root):
    path = root / "nested/data"
    if path.is_symlink() or (path.exists() and not path.is_file()):
        raise AssertionError("destination is not a regular file")
    return path.read_bytes() if path.exists() else None


def inspect_directory(root):
    entries = []
    for path in sorted((root / "nested").iterdir()):
        if path.is_symlink() or not path.is_file():
            raise AssertionError("unexpected non-regular entry in publication directory")
        if path.name != "data" and not path.name.startswith(".ludus-write-"):
            raise AssertionError("unexpected directory entry after recovery")
        entries.append({"name": path.name, **describe(path.read_bytes())})
    return entries


def check_directory(entries, policy, boundary, model):
    is_published = boundary in ("replaced", "directory-synced", "complete")
    is_durable = policy == "directory" and boundary in ("directory-synced", "complete")
    if (model == "process" and is_published) or (model == "storage" and is_durable):
        if [entry["name"] for entry in entries] != ["data"]:
            raise AssertionError("acknowledged rename retained a temporary or lost its destination entry")


def read_destination_inode(root):
    try:
        return (root / "nested/data").stat(follow_symlinks=False).st_ino
    except FileNotFoundError:
        return None


def check_seeded_revision(data, seed_inode, recovered_inode):
    # The Linux replay candidate copies the same seeded ext4/XFS image. A new
    # temporary cannot reuse the still-linked old inode in this bounded workload.
    # Device numbers may change across loop attachments, so compare inode only.
    if seed_inode is not None and recovered_inode == seed_inode and data != OLD:
        raise AssertionError("durably seeded old inode was corrupted")


def check_process(data, replace, shape, boundary):
    # Process kill leaves the kernel/filesystem alive: rename is already visible.
    published = boundary in ("replaced", "directory-synced", "complete")
    expected = payload(shape) if published else (OLD if replace else None)
    if data != expected:
        raise AssertionError(f"process-crash oracle violated at {boundary}")


def check_storage(data, replace, shape, policy, boundary):
    # Declared conservative buffered, journaled-filesystem model: the seed was
    # synced before recording. A rename without directory sync may be lost;
    # publish-only makes no data-durability promise for a recovered new name.
    if boundary in ("directory-synced", "complete") and policy == "directory":
        if data != payload(shape):
            raise AssertionError("acknowledged file+directory sync lost the new destination")
        return
    if boundary not in ("temporary-closed", "replaced", "directory-synced", "complete"):
        if data != (OLD if replace else None):
            raise AssertionError("destination changed before any rename could persist")
        return
    if data == (OLD if replace else None):
        return
    if data is None:
        # Without a directory sync, this model deliberately allows name loss.
        return
    if policy == "publish":
        return
    if data != payload(shape):
        raise AssertionError("file-synced recovered destination contains incomplete/newly mixed data")


def describe(data):
    return {"present": data is not None, "bytes": len(data) if data is not None else 0,
            "sha256": hashlib.sha256(data).hexdigest() if data is not None else None}


def drive(probe, root, policy, shape, callback, timeout_seconds=15):
    child = subprocess.Popen([str(probe), str(root), policy, shape], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
    trace = []
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(child.stdout, selectors.EVENT_READ)
            for expected in boundaries(policy, shape):
                if not selector.select(timeout_seconds):
                    raise RuntimeError(f"probe stalled before {expected}")
                boundary = child.stdout.readline().strip()
                if boundary != expected:
                    raise RuntimeError(f"expected {expected}, received {boundary!r}")
                trace.append(boundary)
                if callback(boundary):
                    child.send_signal(signal.SIGKILL)
                    child.wait(timeout=timeout_seconds)
                    if child.returncode != -signal.SIGKILL:
                        raise RuntimeError("probe did not die from the requested signal")
                    return trace
                child.stdin.write("\n")
                child.stdin.flush()
        if child.wait(timeout=timeout_seconds) != 0:
            raise RuntimeError(f"probe failed: {child.stderr.read()}")
        return trace
    finally:
        if child.poll() is None:
            child.kill()
            child.wait(timeout=timeout_seconds)
        for stream in (child.stdin, child.stdout, child.stderr):
            stream.close()


def process_cases(probe, output, timeout_seconds=15):
    records = []
    for replace in (False, True):
        for shape in SHAPES:
            for policy in POLICIES:
                for boundary in boundaries(policy, shape):
                    case = output / f"{'replace' if replace else 'create'}-{shape}-{policy}-{boundary}"
                    case.mkdir()
                    seed(case, replace)
                    trace = drive(probe, case, policy, shape, lambda reached: reached == boundary, timeout_seconds)
                    data = observe(case)
                    entries = inspect_directory(case)
                    record = dict(directory_entries=entries, model="process-kill", replace=replace, shape=shape, policy=policy,
                                  boundary=boundary, trace=trace, recovered=describe(data))
                    (case / "trace.json").write_text(json.dumps(record, indent=2) + "\n")
                    check_process(data, replace, shape, boundary)
                    check_directory(entries, policy, boundary, "process")
                    # A killed writer cannot strand a cooperative flock. Its
                    # abandoned private temporary must not block a new Publish.
                    drive(probe, case, policy, shape, lambda reached: False, timeout_seconds)
                    if observe(case) != payload(shape):
                        raise AssertionError("republication after process death failed")
                    records.append(record)
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--boundary-timeout-seconds", type=int, default=15)
    args = parser.parse_args()
    if not 1 <= args.boundary_timeout_seconds <= 60:
        parser.error("boundary timeout must be 1..60 seconds")
    args.output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="process-", dir=args.output))
    probe = args.probe.resolve(strict=True)
    records = process_cases(probe, run, args.boundary_timeout_seconds)
    repository = Path(__file__).resolve().parents[2]
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repository, text=True).strip()
    report = {"boundary_timeout_seconds": args.boundary_timeout_seconds, "baseline_revision": revision, "probe_sha256": hashlib.sha256(probe.read_bytes()).hexdigest(),
              "source_sha256": {str(path): hashlib.sha256((repository / path).read_bytes()).hexdigest() for path in
                                (Path("modules/foundation/filesystem/src/persistence_posix.cpp"),
                                 Path("modules/foundation/filesystem/tests/recovery_probe.cpp"))},
              "model": "process-kill (kernel and device remain alive)", "host": platform.platform(),
              "cases": records, "storage_recovery": "NOT TESTED"}
    (run / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"{len(records)} process-crash cases passed; storage recovery NOT TESTED; report: {run / 'report.json'}")


if __name__ == "__main__":
    main()
