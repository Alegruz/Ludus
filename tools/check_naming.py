#!/usr/bin/env python3
"""Enforce the mechanically checkable Ludus naming and boolean rules.

Rules (AGENTS.md "Naming"), checked only in the opt-in scopes listed in
config/naming_scopes.txt so existing code is migrated when it is touched
rather than all at once:

  N1  Use `condition == false` instead of `!condition`.
  N2  Non-const reference parameters start with `out` or `inout`.
  N3  Boolean variables, members and parameters read as questions
      (is/has/should/can/was/are/does/did prefix, optional `m` member prefix).
  N4  Pointer parameters defaulted to nullptr end in `OrNull`.

Private member functions in camelCase are enforced by clang-tidy
(readability-identifier-naming in the scoped .clang-tidy files). Rules that need
type or intent information (bare `condition == true`, verb-first names, no
abbreviations, out vs inout choice, non-defaulted nullable pointers) are
review-only.

The scan is textual on clang-formatted code: comments, string and character
literals are blanked first. Exit code 0 = clean, 1 = violations.

Usage: check_naming.py [repo_root]
"""

import re
import sys
from pathlib import Path

SOURCE_SUFFIXES = {".h", ".hpp", ".cpp"}
QUESTION = re.compile(r"^m?(is|has|should|can|was|are|does|did|Is|Has|Should|Can|Was|Are|Does|Did)[A-Z0-9]")

NEGATION = re.compile(r"!(?!=)\s*[A-Za-z_(]")
# `Type& name` followed by , ) or = in a parameter list (after `(` or `,`).
REFERENCE_PARAMETER = re.compile(r"(?:\(|,)\s*((?:[\w:]+(?:<[^()]*?>)?)(?:\s*\[[^\]]*\])?)&\s+([A-Za-z_]\w*)\s*(?=[,)=])")
BOOLEAN_DECLARATION = re.compile(r"\bbool\s+([A-Za-z_]\w*)\s*(?=[=;{,)\[])")
NULL_DEFAULT_POINTER = re.compile(r"\*\s*([A-Za-z_]\w*)\s*=\s*nullptr\s*(?=[,)])")


def blank_comments_and_literals(text: str) -> str:
    """Replace comments and literals with spaces, keeping offsets and newlines."""
    out = []
    i = 0
    length = len(text)
    while i < length:
        two = text[i:i + 2]
        if two == "//":
            end = text.find("\n", i)
            end = length if end < 0 else end
            out.append(" " * (end - i))
            i = end
        elif two == "/*":
            end = text.find("*/", i + 2)
            end = length if end < 0 else end + 2
            out.append(re.sub(r"[^\n]", " ", text[i:end]))
            i = end
        elif text[i] in "\"'":
            quote = text[i]
            j = i + 1
            while j < length and text[j] != quote:
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, length)
            out.append(quote + " " * (j - i - 2) + quote if j - i >= 2 else text[i:j])
            i = j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def load_scopes(root: Path) -> list[Path]:
    scopes = []
    config = root / "config" / "naming_scopes.txt"
    for line in config.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            scopes.append(root / line)
    return scopes


def scoped_files(root: Path) -> list[Path]:
    files = set()
    for scope in load_scopes(root):
        if scope.is_file():
            files.add(scope)
        elif scope.is_dir():
            files.update(p for p in scope.rglob("*") if p.suffix in SOURCE_SUFFIXES)
        else:
            raise SystemExit(f"naming scope does not exist: {scope.relative_to(root)}")
    return sorted(files)


def check_file(root: Path, path: Path) -> list[str]:
    text = blank_comments_and_literals(path.read_text())
    findings = []

    def report(offset: int, rule: str, message: str) -> None:
        line = text.count("\n", 0, offset) + 1
        findings.append(f"{path.relative_to(root)}:{line}: [{rule}] {message}")

    for match in NEGATION.finditer(text):
        if text[match.start() - 1:match.start()] in {"!", "="}:
            continue
        report(match.start(), "N1", "write `condition == false` instead of `!condition`")
    for match in REFERENCE_PARAMETER.finditer(text):
        type_text, name = match.group(1), match.group(2)
        prefix = text[max(0, match.start() - 16):match.start(1)]
        if re.search(r"\bconst\s*$", prefix) or type_text.startswith("const"):
            continue
        if re.match(r"^(out|inout)[A-Z]", name):
            continue
        report(match.start(2), "N2", f"non-const reference parameter `{name}` needs an out/inout prefix")
    for match in BOOLEAN_DECLARATION.finditer(text):
        name = match.group(1)
        if name == "operator" or QUESTION.match(name) or re.match(r"^(out|inout)(Is|Has|Should|Can|Was|Are|Does|Did)[A-Z]", name):
            continue
        report(match.start(1), "N3", f"boolean `{name}` should read as a question (is/has/should/can...)")
    for match in NULL_DEFAULT_POINTER.finditer(text):
        name = match.group(1)
        if not name.endswith("OrNull"):
            report(match.start(1), "N4", f"nullable pointer parameter `{name}` must end in OrNull")
    return findings


def main() -> int:
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    findings = []
    files = scoped_files(root)
    for path in files:
        findings.extend(check_file(root, path))
    for finding in findings:
        print(finding)
    if findings:
        print(f"Naming check: {len(findings)} violation(s). See AGENTS.md \"Naming\".")
        return 1
    print(f"Naming check OK ({len(files)} scoped files).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
