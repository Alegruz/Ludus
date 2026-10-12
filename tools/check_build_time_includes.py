"""Reject direct standard formatting and heavy public headers before compilation."""
import re
import sys
from pathlib import Path

FORMATTING = re.compile(r"\bstd\s*::\s*(?:format(?:_to(?:_n)?|_string|_args)?|formatted_size|vformat(?:_to)?|make_(?:w)?format_args|wformat_args|basic_format_(?:arg|args|context|string))\b")
INCLUDE = re.compile(r"^\s*#\s*include\s*<([^>]+)>", re.MULTILINE)
HEAVY = {"format", "chrono", "filesystem", "regex", "iostream", "sstream"}
ROOTS = ("modules", "apps", "examples", "tools", "tests")
EXTENSIONS = {".h", ".hpp", ".cpp", ".cc", ".cxx", ".hh", ".hxx"}


def violations(root):
    result = []
    for directory in ROOTS:
        for path in sorted((root / directory).rglob("*")):
            if path.suffix not in EXTENSIONS or not path.is_file():
                continue
            relative = path.relative_to(root)
            if any(part in {"out", "build", "third_party"} for part in relative.parts):
                continue
            text = path.read_text(encoding="utf-8")
            # Ignore comments; keeping literals avoids treating a string's // as a comment.
            text = re.sub(r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')|/\*.*?\*/|//[^\n]*',
                          lambda match: match.group(1) or "", text, flags=re.DOTALL)
            headers = set(INCLUDE.findall(text))
            if "format" in headers or FORMATTING.search(re.sub(r'"(?:\\.|[^"\\])*"', '""', text)):
                result.append(f"{relative}: standard formatting is banned; use the bounded Ludus formatter")
            if "include" in relative.parts and relative.parts[0] == "modules" and headers & HEAVY:
                result.append(f"{relative}: heavy public includes: {', '.join(sorted(headers & HEAVY))}")
    return result


if __name__ == "__main__":
    problems = violations(Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1])
    if problems:
        raise SystemExit("\n".join(problems))
    print("Build-time include policy OK.")
