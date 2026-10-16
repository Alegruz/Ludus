"""Linux-only, opt-in bounded block-prefix recovery on harness-owned images.

Thanks to the kernel dm-log-writes authors and Josef Bacik's replay-log tool:
https://docs.kernel.org/admin-guide/device-mapper/log-writes.html
https://github.com/josefbacik/log-writes/blob/master/replay-log.c
Uses completion/flush-ordered block replay, not process kill as power-loss proof.
No third-party code is copied; install replay-log separately in a disposable VM.
"""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import uuid

from recovery import POLICIES, SHAPES, check_directory, check_storage, describe, drive, inspect_directory, observe, seed


def command(*arguments):
    return subprocess.run([str(value) for value in arguments], check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120).stdout.strip()


@contextmanager
def loop(image):
    device = command("losetup", "--find", "--show", image)
    try:
        yield device
    finally:
        command("losetup", "--detach", device)


@contextmanager
def mounted(device, destination):
    command("mount", "-o", "noatime,nodiscard", device, destination)
    try:
        yield destination
    finally:
        command("umount", destination)


def copy_image(source, destination):
    # Destination is always an exclusive path in our newly created run directory.
    if destination.exists():
        destination.unlink()
    command("cp", "--reflink=auto", "--sparse=always", source, destination)


def make_image(path, size):
    with path.open("xb") as stream:
        stream.truncate(size)


def find_mark(tool, log, mark):
    return int(command(tool, "--log", log, "--find", "--end-mark", mark))


def storage_case(args, directory, filesystem, replace, shape, policy):
    baseline = directory / "baseline.img"
    recorded = directory / "recorded.img"
    log = directory / "writes.log"
    replay = directory / "replay.img"
    mountpoint = directory / "mount"
    mountpoint.mkdir()
    make_image(baseline, 512 * 1024 * 1024)
    # Only newly created regular files are formatted, never a supplied device.
    command(f"mkfs.{filesystem}", "-F" if filesystem == "ext4" else "-f", baseline)
    with loop(baseline) as device:
        with mounted(device, mountpoint):
            seed(mountpoint, replace)
    copy_image(baseline, recorded)
    make_image(log, 256 * 1024 * 1024)
    mapping = "ludus-recovery-" + uuid.uuid4().hex
    trace = []
    with loop(recorded) as data_device, loop(log) as log_device:
        sectors = command("blockdev", "--getsz", data_device)
        command("dmsetup", "create", mapping, "--table",
                f"0 {sectors} log-writes {data_device} {log_device}")
        try:
            with mounted("/dev/mapper/" + mapping, mountpoint):
                (directory / "mount.txt").write_text(command("findmnt", "-T", mountpoint, "-o", "SOURCE,FSTYPE,OPTIONS") + "\n")
                command("dmsetup", "message", mapping, "0", "mark", "start")

                def mark(boundary):
                    command("dmsetup", "message", mapping, "0", "mark", boundary)
                    trace.append(boundary)
                    (directory / "boundaries.json").write_text(json.dumps(trace, indent=2) + "\n")
                    return False

                drive(args.probe, mountpoint, policy, shape, mark)
        finally:
            command("dmsetup", "remove", mapping)
    # Every candidate starts with the same synced baseline, including candidates
    # after a previous recovery mount changed the journal. Unmounting the recorder
    # only drains the log; entries after the complete mark are not replayed.
    marks = {name: find_mark(args.replay_log, log, name) for name in ("start", *trace)}
    start, end = marks["start"], marks["complete"]
    if end - start + 1 > args.max_prefixes:
        raise RuntimeError(f"{end-start+1} prefixes exceed declared bound {args.max_prefixes}; increase it explicitly")
    result = {"model": "dm-log-writes completion/flush-ordered prefixes; no sector tearing or permutation",
              "filesystem": filesystem, "mount_options": "noatime,nodiscard (other defaults recorded in commands.log)",
              "replace": replace, "shape": shape, "policy": policy, "marks": marks, "prefixes": []}
    (directory / "case.json").write_text(json.dumps(result, indent=2) + "\n")
    for entry in range(start, end + 1):
        copy_image(baseline, replay)
        command(args.replay_log, "--log", log, "--replay", replay, "--limit", entry + 1)
        # A pending interval may include the next operation's blocks. Only marks
        # already reached can establish an acknowledged durability guarantee.
        if entry >= marks.get("directory-synced", end + 1):
            boundary = "directory-synced"
        elif entry >= marks["temporary-closed"]:
            boundary = "replaced"
        else:
            boundary = "temporary-created"
        with loop(replay) as device:
            with mounted(device, mountpoint):
                data = observe(mountpoint)
                entries = inspect_directory(mountpoint)
                record = dict(entry=entry, boundary=boundary, recovered=describe(data), directory_entries=entries)
                result["prefixes"].append(record)
                # Write evidence before the oracle; failing replay.img/log/seed
                # remain available for exact reproduction.
                (directory / "case.json").write_text(json.dumps(result, indent=2) + "\n")
                check_storage(data, replace, shape, policy, boundary)
                check_directory(entries, policy, boundary, "storage")
    result["log_sha256"] = hash_file(log)
    # Successful cases retain traces/log plus baseline hash, not three 512MiB
    # scratch copies. Failures leave all images for reproduction.
    result["baseline_sha256"] = hash_file(baseline)
    (directory / "case.json").write_text(json.dumps(result, indent=2) + "\n")
    for path in (baseline, recorded, replay):
        path.unlink()
    return result


def hash_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    global command
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--replay-log", type=Path, required=True)
    parser.add_argument("--filesystem", choices=("ext4", "xfs", "both"), default="both")
    parser.add_argument("--max-prefixes", type=int, default=256)
    parser.add_argument("--disposable-vm", action="store_true", required=True,
                        help="acknowledge this root-only experiment runs in a disposable Linux VM")
    args = parser.parse_args()
    if platform.system() != "Linux" or os.geteuid() != 0:
        parser.error("requires root in a disposable Linux VM; this is not a host process-crash test")
    if not 1 <= args.max_prefixes <= 4096:
        parser.error("max-prefixes must be 1..4096")
    args.probe = args.probe.resolve(strict=True)
    args.replay_log = args.replay_log.resolve(strict=True)
    for tool in ("losetup", "dmsetup", "blockdev", "mount", "umount", "cp", "findmnt"):
        if shutil.which(tool) is None:
            parser.error(f"missing {tool}")
    filesystems = ("ext4", "xfs") if args.filesystem == "both" else (args.filesystem,)
    for filesystem in filesystems:
        if shutil.which(f"mkfs.{filesystem}") is None:
            parser.error(f"missing mkfs.{filesystem}")
    if "log-writes" not in command("dmsetup", "targets"):
        parser.error("dm-log-writes kernel target unavailable; no coverage claimed")
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="block-", dir=args.output.resolve()))
    report = {"host": platform.platform(), "baseline_revision": command("git", "-c", f"safe.directory={Path.cwd().resolve()}", "rev-parse", "HEAD"),
              "crash_model": "bounded block-prefix replay; device honors flush/FUA; not exhaustive power loss",
              "max_prefixes_per_case": args.max_prefixes, "probe_sha256": hash_file(args.probe),
              "source_sha256": {str(path): hash_file(path) for path in (Path("modules/foundation/filesystem/src/persistence_posix.cpp"), Path("modules/foundation/filesystem/tests/recovery_probe.cpp"))},
              "replay_log_sha256": hash_file(args.replay_log), "cases": []}
    # Exact commands, mount settings and tool versions are kept in a host-side
    # transcript. Output directories/images never come from a block-device input.
    original = command

    def recorded_command(*arguments):
        with (output / "commands.log").open("a") as stream:
            stream.write(json.dumps([str(value) for value in arguments]) + "\n")
        try:
            completed = original(*arguments)
        except subprocess.CalledProcessError as error:
            with (output / "commands.log").open("a") as stream:
                stream.write(json.dumps({"error": error.returncode, "stdout": error.stdout, "stderr": error.stderr}) + "\n")
            raise
        with (output / "commands.log").open("a") as stream:
            stream.write(json.dumps({"stdout": completed}) + "\n")
        return completed

    command = recorded_command
    (output / "environment.txt").write_text(command("uname", "-a") + "\n" + command("mount") + "\n")
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    for filesystem in filesystems:
        for replace in (False, True):
            for shape in SHAPES:
                for policy in POLICIES:
                    directory = output / f"{filesystem}-{'replace' if replace else 'create'}-{shape}-{policy}"
                    directory.mkdir()
                    result = storage_case(args, directory, filesystem, replace, shape, policy)
                    report["cases"].append(result)
                    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"block-prefix cases passed: {len(report['cases'])}; evidence: {output}")


if __name__ == "__main__":
    main()
