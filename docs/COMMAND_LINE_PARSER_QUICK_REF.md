# Command-Line Parser Quick Reference

Status: reference
Owner: core maintainers
Last updated: 2026-01-26

---

## Quick Examples

### Basic
```cpp
CommandLineManager<char> manager =
    CommandLineManager<char>::Create("program arg1 arg2 arg3");
// Result: ["program", "arg1", "arg2", "arg3"]
```

### Spaces in arguments
```cpp
CommandLineManager<char> manager =
    CommandLineManager<char>::Create("program \"hello world\" arg2");
// Result: ["program", "hello world", "arg2"]
```

### Escapes
```cpp
CommandLineManager<wchar_t> manager =
    CommandLineManager<wchar_t>::Create(
        L"copy \"C:\\Program Files\\src.txt\" \"C:\\Program Files\\dst.txt\"");
// Result: ["copy", "C:\\Program Files\\src.txt", "C:\\Program Files\\dst.txt"]
```

---

## Escape Reference

| Input | Output | Use case |
|-------|--------|----------|
| `\\` | `\` | backslash |
| `\"` | `"` | quote inside quotes |
| `\'` | `'` | apostrophe inside quotes |
| `\n` | newline | multi-line values |
| `\t` | tab | tabbed values |
| `\r` | CR | carriage return |
| `\ ` | space | space outside quotes |

---

## Notes

- Empty quotes produce empty arguments.
- Unclosed quotes consume the rest of the line.
- Unknown escapes preserve the backslash (e.g., `\x`).

---

## Implementation and Tests

- Implementation: `include/Ludus/Engine/Core/CommandLineManager.hpp`
- Tests: `src/Engine/Core/CoreTests.cpp`
- Validator: `tools/validate_parser.py`
