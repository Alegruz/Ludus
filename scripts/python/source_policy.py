"""Review-derived source rules; changed-code ratchet, without a debt baseline."""

from __future__ import annotations

import argparse
import difflib
import re
import subprocess
from pathlib import Path

from formatting import TOKENS, format_source

CPP = {".cpp", ".cc", ".cxx", ".h", ".hpp", ".mm"}
ROOTS = {"modules", "apps", "examples", "tests", "tools"}
BINDING_BOUNDARY = "modules/graphics/rhi/shaders/raster_bindings.slang"


def code_only(source: str) -> str:
    """Preserve offsets/newlines while removing comments, literals and directives."""
    pieces = []
    end = 0
    for match in TOKENS.finditer(source):
        token = match.group()
        pieces.append(source[end : match.start()])
        if (
            token.startswith(("//", "/*"))
            or token.lstrip().startswith("#")
            or '"' in token
            or "'" in token
        ):
            pieces.append(re.sub(r"[^\n]", " ", token))
        else:
            pieces.append(token)
        end = match.end()
    pieces.append(source[end:])
    return "".join(pieces)


def added_lines(before: str, after: str) -> set[int]:
    lines = set()
    for operation, _, _, start, end in difflib.SequenceMatcher(
        None, before.splitlines(), after.splitlines(), autojunk=False
    ).get_opcodes():
        if operation in {"insert", "replace"}:
            lines.update(range(start + 1, end + 1))
    return lines


def violations(path: str, source: str, changed: set[int]) -> list[tuple[int, str]]:
    """Check lexical conventions, not semantic coordinate correctness or C++ types."""
    code = code_only(source)
    found = []

    def report(offset: int, message: str) -> None:
        line = code.count("\n", 0, offset) + 1
        if line in changed:
            found.append((line, message))

    if Path(path).suffix in CPP:
        for match in re.finditer(
            r"\bauto(?:\s+(?:const|volatile))*\s*[*&]*\s+(\w+|\[[^\]]+\])\s*(=|:|\(|\{)",
            code,
        ):
            # Lambda closure types cannot be spelled; generic parameters and
            # trailing return types are not variable declarations matched here.
            tail = code[match.end() :].lstrip()
            if match[2] == "(":
                depth, end = 1, match.end()
                while depth and end < len(code):
                    depth += (code[end] == "(") - (code[end] == ")")
                    end += 1
                following = code[end:].lstrip()
                if following.startswith(("->", "noexcept", "{")):
                    continue  # Function return deduction is not a variable.
            if match[2] in {"=", "{"} and tail.startswith("["):
                continue
            line = code.count("\n", 0, match.start())
            original = source.splitlines()[line]
            if re.search(
                r"// ludus-style: inferred-type: (iterator|dependent) .{12,}", original
            ):
                continue
            report(
                match.start(),
                "spell the variable type; auto requires an unspellable lambda or justified iterator/dependent type",
            )
        return found
    if not path.endswith(".slang"):
        return found
    if path != BINDING_BOUNDARY:
        # Inspect original annotation tokens, since code_only hides #defines.
        for match in re.finditer(r"\[\[\s*vk::(?:binding|location)\b", code):
            report(
                match.start(),
                "use the RHI shader interface macros, not target annotation vocabulary",
            )
    for declaration in re.finditer(r"\b(?:struct|cbuffer)\s+(\w+)", code):
        if not re.fullmatch(r"[A-Z][A-Za-z0-9]*", declaration[1]):
            report(
                declaration.start(1),
                "shader types and constant-buffer blocks use PascalCase",
            )
    for match in re.finditer(
        r"\b(?:struct|cbuffer)\s+\w+\s*\{([^{}]*)\}", code, re.DOTALL
    ):
        for field in re.finditer(
            r"\b(?:column_major\s+)?\w+(?:<[^;]+>)?\s+(\w+)\s*(?:[:;\[=,])", match[1]
        ):
            offset = match.start(1) + field.start(1)
            name = field[1]
            if not re.fullmatch(r"[A-Z][A-Za-z0-9]*", name):
                report(offset, "shader members use PascalCase")
            if re.search(
                r"Position|Normal|Direction|Eye|Bounds", name
            ) and not re.search(r"Mesh|World|View|Clip|Ndc|Pixel|Uv|Tangent", name):
                report(
                    offset,
                    "coordinate-bearing members name their space (World, View, Clip, Pixel, etc.)",
                )
    for match in re.finditer(
        r"\b(\w+)\s+(\w+)\s*\(([^{};]*)\)\s*(?::\s*\w+)?\s*\{", code
    ):
        if match[1] in {"if", "else", "for", "while", "switch", "return"}:
            continue
        if not re.fullmatch(r"[A-Z][A-Za-z0-9]*", match[2]):
            report(match.start(2), "shader functions and entry points use PascalCase")
        for parameter in re.finditer(
            r"\b(?:in\s+|inout\s+|out\s+)?\w+\s+(\w+)\s*(?:[:,=]|$)", match[3]
        ):
            if not re.fullmatch(r"[a-z][A-Za-z0-9]*", parameter[1]):
                report(
                    match.start(3) + parameter.start(1),
                    "shader parameters use lowerCamelCase",
                )
        for parameter in re.finditer(r"\bout\s+\w+\s+(\w+)", match[3]):
            if not re.fullmatch(r"out[A-Z][A-Za-z0-9]*", parameter[1]):
                report(
                    match.start(3) + parameter.start(1),
                    "out parameters use outPascalCase",
                )
    return found


def check(
    root: Path, base: str, clang_format: str | None = None, head: str | None = None
) -> list[str]:
    """Inspect a PR head tree, or local edits; never attribute synthetic-merge main changes to a PR."""
    if head:
        base = subprocess.run(
            ["git", "merge-base", base, head],
            cwd=root,
            text=True,
            capture_output=True,
            check=True,
        ).stdout.strip()
    comparison = [base, head] if head else [base]
    names = subprocess.run(
        ["git", "diff", "--name-only", "--diff-filter=ACMR", *comparison],
        cwd=root,
        text=True,
        capture_output=True,
        check=True,
    ).stdout.splitlines()
    if not head:
        names += subprocess.run(
            ["git", "ls-files", "--others", "--exclude-standard"],
            cwd=root,
            text=True,
            capture_output=True,
            check=True,
        ).stdout.splitlines()
    failures = []
    for name in sorted(set(names)):
        path = Path(name)
        if (
            not path.parts
            or path.parts[0] not in ROOTS
            or path.suffix not in CPP | {".slang"}
        ):
            continue
        source = (
            subprocess.run(
                ["git", "show", f"{head}:{name}"],
                cwd=root,
                text=True,
                capture_output=True,
                check=True,
            ).stdout
            if head
            else (root / path).read_text(encoding="utf-8")
        )
        if (
            clang_format
            and format_source(source, (root / path).resolve(), clang_format) != source
        ):
            failures.append(
                f"{name}: source formatting differs; use the pinned formatter and formatting.format_source"
            )
        previous = subprocess.run(
            ["git", "show", f"{base}:{name}"], cwd=root, text=True, capture_output=True
        )
        before = previous.stdout if previous.returncode == 0 else ""
        for line, message in violations(name, source, added_lines(before, source)):
            failures.append(f"{name}:{line}: {message}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--base", required=True, help="PR base commit, or explicit local comparison ref"
    )
    parser.add_argument(
        "--clang-format",
        help="also verify formatting of every changed C++/Slang source",
    )
    parser.add_argument(
        "--head",
        help="inspect this exact PR head instead of the checked-out synthetic merge",
    )
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    failures = check(root, arguments.base, arguments.clang_format, arguments.head)
    for failure in failures:
        print(failure)
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
