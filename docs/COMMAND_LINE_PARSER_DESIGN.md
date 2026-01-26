# Command-Line Parser Design Document

Status: design
Owner: core maintainers
Last updated: 2026-01-26

---

## Overview

This document explains the technical design and implementation choices for the enhanced command-line parser in `CommandLineManager`.

## Problem Statement

The original parser only supported space-splitting, making it impossible to handle:
- Arguments with spaces (e.g., file paths)
- Quoted strings
- Escape sequences
- Special characters in arguments

This was a significant UX limitation for an engine/editor that needs to accept complex command-line parameters.

## Solution Design

### Core Principle
Implement a state machine-based parser that is:
- Simple: easy to understand and maintain
- Efficient: single O(n) pass through input
- Robust: handles edge cases gracefully
- Compatible: does not break existing code

### State Machine

```
+------------------+
|   Normal State   |
| (read characters)|
+------------------+
        |
        | quote seen
        v
+------------------+
|   Quote State    |
| (read all chars) |
+------------------+
        |
        | matching quote
        v
+------------------+
|   Escape State   |
| (interpret char) |
+------------------+
```

### State Variables

```cpp
bool inQuotes = false;          // Are we inside a quoted string?
CharT quoteChar = '\0';         // Which quote opened this string?
bool justClosedQuotes = false;  // Did we just exit a quote?
```

The `justClosedQuotes` flag is crucial for handling empty quoted arguments like `""` or `''`.

### Algorithm Pseudocode

```
while current character is not null:
    if current is backslash AND next char exists:
        handle escape sequence
        continue

    if current is quote AND not in quotes:
        enter quote mode with this quote type
        continue

    if current is matching quote AND in quotes:
        exit quote mode
        mark that we just closed quotes
        continue

    if current is space AND not in quotes:
        skip consecutive spaces
        if we have argument OR just closed quotes:
            push argument to result
        continue

    append current character to argument
    advance by 1

if argument not empty OR just closed quotes:
    push final argument
```

## Design Decisions

### 1. Quote Symmetry
Decision: support both `"..."` and `'...'` equally
Rationale: aligns with common shell conventions

### 2. Escape Sequences
Decision: support `\n`, `\t`, `\r`, `\\`, `\"`, `\'`, and `\ `
Rationale: needed for special characters and multi-line values

### 3. Space Escaping Outside Quotes
Decision: allow `\ ` to escape spaces outside quotes
Rationale: enables advanced usage without requiring quotes

### 4. Empty Quotes as Valid Arguments
Decision: `""` produces an empty string argument
Rationale: allows explicit empty values

### 5. Single Pass Algorithm
Decision: process input in one pass, no preprocessing
Rationale: O(n) efficiency with no intermediate buffers

### 6. DynamicArray for Current Argument
Decision: use `DynamicArray<CharT>` while building each argument
Rationale: avoids pre-counting and keeps memory ownership simple

### 7. Character Type Templates
Decision: support both `char` and `wchar_t`
Rationale: works with both ASCII and Windows wide command lines

## Trade-offs

### Simplicity vs. Features
Trade-off: limited escape sequences vs. full shell support
Justification: balance simplicity for maintainability

### Strictness vs. Robustness
Trade-off: accept unclosed quotes vs. reject them
Justification: treat unclosed quotes as part of the argument to be forgiving

### Performance vs. Validation
Trade-off: no detailed error reporting
Justification: keep parsing fast and predictable

## Memory Model

### Stack Usage
```
inQuotes:         1 byte
quoteChar:        1-2 bytes (char or wchar_t)
justClosedQuotes: 1 byte
current:          8 bytes (pointer)
```

### Heap Usage
- `currentArgument`: grows as needed for the current argument
- `arguments`: one entry per parsed argument
- No deep copies; strings are moved into the result

## Backward Compatibility

- `Create(int argc, char** argv)` unchanged
- Behavior for simple space-separated arguments is identical

## Testing Strategy

- Unit tests for basic parsing, quotes, escapes, edge cases
- Python reference validator in `tools/validate_parser.py`

## Known Limitations

- No nested quotes without escaping
- No globbing or variable expansion
- Unknown escapes preserve the backslash

## Maintenance Guidelines

- When adding escape sequences, update docs and tests
- Keep the algorithm single-pass and deterministic

## References

- Public API: `include/Ludus/Engine/Core/CommandLineManager.h`
- Implementation: `include/Ludus/Engine/Core/CommandLineManager.hpp`
- Tests: `src/Engine/Core/CoreTests.cpp`
- Validator: `tools/validate_parser.py`
