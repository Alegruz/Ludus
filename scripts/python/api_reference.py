"""Public SDK inventory, pinned Doxygen build and documentation coverage gate."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import zipfile
import urllib.request
import xml.etree.ElementTree as ET

from api_markdown import render_reference

ROOT = Path(__file__).resolve().parents[2]
TEMPLATES = {
    "build_metadata.hpp": "cmake/build_metadata.hpp.in",
    "assert_config.hpp": "cmake/assert_config.hpp.in",
    "profiling_config.hpp": "cmake/profiling_config.hpp.in",
}


def public_inputs(root):
    """Read the repository's literal public FILE_SET declarations, failing closed.

    This is intentionally not a general CMake evaluator. Variable/glob-based
    input changes require updating this reader, rather than silently broadening
    publication to private source. Generated policy headers use source templates.
    """
    inputs = set()
    declared = set()
    for cmake in sorted((root / "modules").rglob("CMakeLists.txt")):
        text = re.sub(r"#[^\n]*", "", cmake.read_text())
        for command in re.findall(r"target_sources\s*\((.*?)\)", text, re.S):
            for block in re.findall(r"(?:PUBLIC|INTERFACE)\s+FILE_SET\s+public_headers\s+TYPE\s+HEADERS\s+(.*)", command, re.S):
                if "FILES" not in block:
                    raise ValueError(f"Unsupported public file set: {cmake}")
                files = re.split(r"\bFILES\b", block, maxsplit=1)[1]
                for token in re.findall(r'"[^"\n]*"|[^\s]+', files):
                    token = token.strip('"')
                    if token in {"PRIVATE", "PUBLIC", "INTERFACE", "FILE_SET"}:
                        raise ValueError(f"Unsupported mixed public file set: {cmake}")
                    if token.startswith("${"):
                        template = TEMPLATES.get(Path(token).name)
                        if template is None:
                            raise ValueError(f"Unknown generated public header: {token}")
                        inputs.add(root / template)
                        continue
                    path = cmake.parent / token
                    if not token.startswith("include/") or path.suffix not in {".h", ".hpp"}:
                        raise ValueError(f"Unsupported public header: {token}")
                    if not path.is_file() or path.is_symlink():
                        raise ValueError(f"Missing or symlinked public header: {path}")
                    declared.add(path)
                    if not {"detail", "internal"}.intersection(path.parts):
                        inputs.add(path)
    discovered = {p for p in (root / "modules").rglob("*")
                  if p.suffix in {".h", ".hpp"} and "include" in p.relative_to(root / "modules").parts}
    if discovered - declared:
        raise ValueError("Headers missing from literal public file sets: " +
                         ", ".join(str(p.relative_to(root)) for p in sorted(discovered - declared)))
    if not inputs or any(not p.is_file() for p in inputs):
        raise ValueError("Empty or incomplete public header inventory")
    return sorted(inputs)


def documented(node):
    return any("".join(node.find(tag).itertext()).strip()
               for tag in ("briefdescription", "detaileddescription") if node.find(tag) is not None)


def coverage(xml):
    """Use semantic keys, not Doxygen's generated IDs, to track the backlog."""
    missing = set()
    symbols = set()
    for entry in ET.parse(xml / "index.xml").getroot().findall("compound"):
        if entry.get("kind") not in {"class", "struct", "namespace", "file", "union", "concept"}:
            continue
        compound = ET.parse(xml / (entry.get("refid") + ".xml")).getroot().find("compounddef")
        name = compound.findtext("compoundname", "")
        if compound.get("kind") in {"class", "struct", "union", "concept"}:
            key = f"{compound.get('kind')} {name}"
            symbols.add(key)
            if not documented(compound):
                missing.add(key)
        for member in compound.findall("sectiondef/memberdef"):
            if member.get("prot") not in {None, "public"}:
                continue
            qualified = member.findtext("qualifiedname") or name + "::" + member.findtext("name", "")
            args = " ".join(member.findtext("argsstring", "").split())
            key = f"{member.get('kind')} {qualified}{args}"
            symbols.add(key)
            if not documented(member):
                missing.add(key)
            for value in member.findall("enumvalue"):
                value_key = f"enumvalue {qualified}::{value.findtext('name')}"
                symbols.add(value_key)
                if not documented(value):
                    missing.add(value_key)
    return symbols, missing


def check_coverage(missing, baseline):
    return sorted(missing - set(baseline))


def check_extracted_files(root, xml, inputs):
    expected = {str(path.relative_to(root)) for path in inputs}
    actual = set()
    for entry in ET.parse(xml / "index.xml").getroot().findall("compound"):
        if entry.get("kind") == "file":
            compound = ET.parse(xml / (entry.get("refid") + ".xml")).getroot().find("compounddef")
            actual.add(compound.find("location").get("file"))
    if actual != expected:
        raise ValueError(f"Extracted file inventory mismatch: missing {sorted(expected - actual)}, "
                         f"unexpected {sorted(actual - expected)}")


def bootstrap(root, pin):
    """Install only the verified CLI member; leave an existing tool intact on failure."""
    system, machine = platform.system(), platform.machine().lower()
    if system == "Linux" and machine in {"x86_64", "amd64"}:
        selected = {"url": pin["linux_x64_url"], "sha256": pin["sha256"],
                    "archive_binary": pin["archive_binary"]}
        archive_name = "doxygen.tar.gz"
    elif system == "Darwin" and machine in {"arm64", "aarch64", "x86_64", "amd64"}:
        cpu = "arm64" if machine in {"arm64", "aarch64"} else "x64"
        selected = pin["macos_" + cpu]
        minimum = tuple(int(part) for part in selected["minimum_macos"].split("."))
        version = platform.mac_ver()[0]
        if not re.fullmatch(r"[0-9]+(?:\.[0-9]+)*", version):
            raise ValueError("Cannot determine macOS version for Doxygen bootstrap; use --doxygen")
        installed = tuple(int(part) for part in version.split("."))
        if installed[:len(minimum)] < minimum:
            raise ValueError(f"Pinned Doxygen bootstrap requires macOS {selected['minimum_macos']} or later; "
                             "use --doxygen with a compatible build of the pinned version on older hosts")
        archive_name = "doxygen-macos-" + cpu + ".zip"
    else:
        raise ValueError("Automatic Doxygen bootstrap supports Linux x64 and macOS ARM64/x64; "
                         "use --doxygen on other hosts")
    # Thanks to Dimitri van Heesch / Doxygen, Download and its SHA-256 table:
    # https://www.doxygen.nl/download.html. Use native CLI ZIPs instead of mounting DMGs.
    directory = root / "out/doxygen-tools"
    directory.mkdir(parents=True, exist_ok=True)
    archive = directory / archive_name
    binary = directory / "bin/doxygen"
    limit = 256 * 1024 * 1024

    def digest(path):
        with path.open("rb") as source:
            return hashlib.file_digest(source, "sha256").hexdigest()

    def copy_bounded(source, output):
        total = 0
        while chunk := source.read(1024 * 1024):
            total += len(chunk)
            if total > limit:
                raise ValueError("Doxygen download or binary exceeds size limit")
            output.write(chunk)

    with tempfile.TemporaryDirectory(prefix="staging-", dir=directory) as temporary:
        staging = Path(temporary)
        if not archive.is_file() or archive.stat().st_size > limit or digest(archive) != selected["sha256"]:
            candidate = staging / archive_name
            request = urllib.request.Request(selected["url"], headers={"User-Agent": "Ludus-docs/1.0"})
            with urllib.request.urlopen(request, timeout=120) as response, candidate.open("wb") as output:
                copy_bounded(response, output)
            if digest(candidate) != selected["sha256"]:
                raise ValueError("Doxygen archive SHA-256 mismatch")
            candidate.replace(archive)
        candidate = staging / "doxygen"
        member = selected["archive_binary"]
        if archive_name.endswith(".zip"):
            with zipfile.ZipFile(archive) as package:
                entries = [entry for entry in package.infolist() if entry.filename == member]
                if len(entries) != 1:
                    raise ValueError("Doxygen archive must contain exactly one CLI binary")
                entry = entries[0]
                mode = entry.external_attr >> 16
                if entry.is_dir() or stat.S_IFMT(mode) not in {0, stat.S_IFREG} or entry.file_size > limit:
                    raise ValueError("Doxygen binary is not a bounded regular archive member")
                with package.open(entry) as source, candidate.open("wb") as output:
                    copy_bounded(source, output)
        else:
            with tarfile.open(archive) as package:
                entries = [entry for entry in package.getmembers() if entry.name == member]
                if len(entries) != 1:
                    raise ValueError("Doxygen archive must contain exactly one CLI binary")
                entry = entries[0]
                if not entry.isfile() or entry.size > limit:
                    raise ValueError("Doxygen binary is not a bounded regular archive member")
                with package.extractfile(entry) as source, candidate.open("wb") as output:
                    copy_bounded(source, output)
        candidate.chmod(0o755)
        binary.parent.mkdir(exist_ok=True)
        candidate.replace(binary)
    return str(binary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootstrap", action="store_true", help="Download the hash-pinned official binary for Linux x64 or macOS ARM64/x64")
    parser.add_argument("--doxygen", default="doxygen", help="Path to the pinned Doxygen executable")
    parser.add_argument("--update-baseline", action="store_true", help="Explicitly replace the reviewed legacy gap baseline")
    args = parser.parse_args()
    pin = json.loads((ROOT / "config/doxygen_toolchain.json").read_text())
    executable = bootstrap(ROOT, pin) if args.bootstrap else args.doxygen
    version = subprocess.check_output([executable, "--version"], text=True).split()[0]
    if version != pin["version"]:
        raise ValueError(f"Doxygen {pin['version']} required; found {version}")
    inputs = public_inputs(ROOT)
    output = ROOT / "out/api-reference"
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    paths = inputs + [ROOT / "docs/api-main.md"]
    config = output / "Doxyfile"
    config.write_text((ROOT / "docs/Doxyfile").read_text() +
                      f'\nOUTPUT_DIRECTORY = "{output}"\n' +
                      "INPUT = " + " ".join('"' + str(p) + '"' for p in paths) + "\n")
    subprocess.run([executable, str(config)], cwd=ROOT, check=True)
    check_extracted_files(ROOT, output / "xml", paths)
    symbols, missing = coverage(output / "xml")
    if not symbols:
        raise ValueError("Doxygen produced no public symbols")
    baseline_path = ROOT / "docs/api-undocumented.json"
    if args.update_baseline:
        baseline_path.write_text(json.dumps(sorted(missing), indent=2) + "\n")
    baseline = json.loads(baseline_path.read_text())
    failures = check_coverage(missing, baseline)
    report = {"doxygen": version, "headers": len(inputs), "symbols": len(symbols),
              "documented": len(symbols - missing), "undocumented": sorted(missing),
              "new_gaps": failures, "resolved_or_removed": sorted(set(baseline) - missing)}
    (output / "coverage.json").write_text(json.dumps(report, indent=2) + "\n")
    if failures:
        raise ValueError("New undocumented public symbols:\n" + "\n".join(failures))
    site = ROOT / "out/wiki"
    if not (site / "index.html").is_file():
        raise ValueError("Build MkDocs before build-api; it cleans the shared output")
    render_reference(ROOT, output / "xml", output / "markdown", paths)
    subprocess.run([sys.executable, "-m", "mkdocs", "build", "--strict"], cwd=ROOT, check=True)
    print(f"API reference: {len(inputs)} headers, {len(symbols)} symbols, "
          f"{len(missing)} legacy documentation gaps; no new gaps.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError, ET.ParseError, tarfile.TarError, zipfile.BadZipFile, KeyError) as error:
        raise SystemExit(f"API documentation: {error}")
