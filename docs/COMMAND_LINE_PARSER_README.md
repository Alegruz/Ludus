# Command-Line Parser

Status: authoritative
Owner: core maintainers
Last updated: 2026-01-26

This document is the canonical guide to the Ludus command-line parser. It covers usage, behavior, and maintenance references.

---

## Overview

`CommandLineManager::Create(CharT* commandLine)` parses a single command line string into arguments. It supports:

- Double-quoted strings: "hello world"
- Single-quoted strings: 'hello world'
- Escape sequences: \n, \t, \r, \\, \", \\', and \ (space)
- Empty quoted arguments: "" or ''

The parser is a single-pass state machine with O(n) time complexity and full backward compatibility with space-splitting.

---

## API

```cpp
template<StringCharType CharT>
static CommandLineManager<CharT> Create(CharT* commandLine) noexcept
```

- `commandLine`: null-terminated string to parse
- Supported `CharT`: `char`, `wchar_t`

```cpp
const DynamicArray<BasicString<CharT>>& GetArguments() const noexcept
```

---

## Parsing Rules

1. Whitespace separates arguments (when not inside quotes).
2. Quotes preserve whitespace inside the quoted section.
3. A backslash introduces an escape sequence:
   - `\n`, `\t`, `\r`, `\\`, `\"`, `\'`
   - `\ ` to escape a space outside quotes
4. Unknown escapes preserve the backslash (e.g., `\x` stays `\x`).
5. Empty quotes produce an empty argument.
6. Unclosed quotes consume the rest of the input.

---

## Examples

### Basic
```cpp
CommandLineManager<char> manager =
    CommandLineManager<char>::Create("program arg1 arg2");
// Result: ["program", "arg1", "arg2"]
```

### Quoted arguments
```cpp
CommandLineManager<char> manager =
    CommandLineManager<char>::Create("program \"hello world\" arg2");
// Result: ["program", "hello world", "arg2"]
```

### Escapes
```cpp
CommandLineManager<char> manager =
    CommandLineManager<char>::Create("program \"line1\\nline2\"");
// Result: ["program", "line1\nline2"]
```

### Escaped space (outside quotes)
```cpp
CommandLineManager<char> manager =
    CommandLineManager<char>::Create("program hello\\ world");
// Result: ["program", "hello world"]
```

---

## Supported Escapes

| Sequence | Result |
|----------|--------|
| `\\` | backslash |
| `\"` | double quote |
| `\'` | single quote |
| `\n` | newline |
| `\t` | tab |
| `\r` | carriage return |
| `\ ` | space |

---

## Limitations

- No nested quotes (must escape inner quotes).
- No variable expansion (`$VAR`).
- No globbing (`*.txt`).
- No brace expansion (`{a,b}`).

These are intentional trade-offs for simplicity and maintainability.

---

## Tests and Validation

C++ unit tests live in `src/Engine/Core/CoreTests.cpp` and include:
- `CommandLineManager_BasicParsing`
- `CommandLineManager_DoubleQuotes`
- `CommandLineManager_SingleQuotes`
- `CommandLineManager_EscapeSequences`
- `CommandLineManager_EscapeQuotes`
- `CommandLineManager_EscapeSpace`
- `CommandLineManager_MixedQuotesAndEscapes`
- `CommandLineManager_MultipleSpaces`
- `CommandLineManager_EmptyQuotes`
- `CommandLineManager_BackslashAtEnd`

Python validation:
```bash
python tools/validate_parser.py
```

---

## Related Docs

- Design: [COMMAND_LINE_PARSER_DESIGN.md](COMMAND_LINE_PARSER_DESIGN.md)
- Quick reference: [COMMAND_LINE_PARSER_QUICK_REF.md](COMMAND_LINE_PARSER_QUICK_REF.md)
