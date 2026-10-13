"""Shared lexical naming rules; callers mask comments/literals before scanning."""
import re

# Require a type before the stars: *required = nullptr writes a pointee,
# and does not establish that required itself is nullable.
NULL_POINTER_DECLARATION = re.compile(
    r"\b(?!(?:return|co_return|delete)\b)[A-Za-z_]\w*(?:::\w+)*(?:<[^;{}()]*>)?\s*"
    r"(?:\*\s*(?:(?:const|volatile)\s+)*)+"
    r"(\w+)\s*(?:=\s*nullptr|\{\s*(?:nullptr)?\s*\})"
)
MOVE_PARAMETER = re.compile(r"(?:\(|,)\s*(?:const\s+)?[\w:]+(?:<[^()]*?>)?\s*&&\s*(\w+)\s*(?=[,)=])")


def pointer_findings(code):
    """Return offsets/rules/messages for explicit null initializers and move parameters.

    This does not infer nullable return values or prove a pointer's lifetime.
    """
    findings = []
    for match in NULL_POINTER_DECLARATION.finditer(code):
        if not match[1].endswith("OrNull"):
            findings.append((match.start(1), "N4", f"nullable pointer `{match[1]}` must end in OrNull"))
    for match in MOVE_PARAMETER.finditer(code):
        if re.match(r"(?:out|inout)(?:[A-Z]|$)", match[1]):
            findings.append((match.start(1), "N2", "rvalue reference parameters must omit out/inout prefixes"))
    return findings
