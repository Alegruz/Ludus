"""Public SDK inventory, pinned Doxygen build and documentation coverage gate."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import urllib.request
import xml.etree.ElementTree as ET

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
    if platform.system() != "Linux" or platform.machine() not in {"x86_64", "AMD64"}:
        raise ValueError("Automatic Doxygen bootstrap supports Linux x64; use --doxygen on other hosts")
    directory = root / "out/doxygen-tools"
    directory.mkdir(parents=True, exist_ok=True)
    archive = directory / "doxygen.tar.gz"
    if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != pin["sha256"]:
        with urllib.request.urlopen(pin["linux_x64_url"], timeout=120) as response, archive.open("wb") as output:
            shutil.copyfileobj(response, output)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != pin["sha256"]:
        raise ValueError("Doxygen archive SHA-256 mismatch")
    binary = directory / "bin/doxygen"
    binary.parent.mkdir(exist_ok=True)
    with tarfile.open(archive) as package:
        entry = package.getmember(pin["archive_binary"])
        if not entry.isfile():
            raise ValueError("Doxygen binary is not a regular archive member")
        with package.extractfile(entry) as source, binary.open("wb") as output:
            shutil.copyfileobj(source, output)
    binary.chmod(0o755)
    return str(binary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootstrap", action="store_true", help="Download the hash-pinned official Linux x64 binary")
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
    destination = site / "api"
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(output / "html", destination)
    print(f"API reference: {len(inputs)} headers, {len(symbols)} symbols, "
          f"{len(missing)} legacy documentation gaps; no new gaps.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError, ET.ParseError, tarfile.TarError) as error:
        raise SystemExit(f"API documentation: {error}")
