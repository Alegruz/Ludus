# P2 Command-Line Parser Upgrade

Status: complete
Last updated: 2026-01-26

This document is a concise completion record for the command-line parser upgrade. Canonical usage and behavior live in:
- [COMMAND_LINE_PARSER_README.md](COMMAND_LINE_PARSER_README.md)
- [COMMAND_LINE_PARSER_QUICK_REF.md](COMMAND_LINE_PARSER_QUICK_REF.md)
- [COMMAND_LINE_PARSER_DESIGN.md](COMMAND_LINE_PARSER_DESIGN.md)

---

## Scope and Outcomes

- Added support for single and double quotes.
- Added escape sequences: \n, \t, \r, \\, \", \\', and \ (space).
- Preserved backward compatibility with space-splitting.
- Implemented a single-pass O(n) state machine.

---

## Tests

C++ tests in `src/Engine/Core/CoreTests.cpp`:
- CommandLineManager_BasicParsing
- CommandLineManager_DoubleQuotes
- CommandLineManager_SingleQuotes
- CommandLineManager_EscapeSequences
- CommandLineManager_EscapeQuotes
- CommandLineManager_EscapeSpace
- CommandLineManager_MixedQuotesAndEscapes
- CommandLineManager_MultipleSpaces
- CommandLineManager_EmptyQuotes
- CommandLineManager_BackslashAtEnd

Python validator:
```bash
python tools/validate_parser.py
```

---

## Files Updated

- `include/Ludus/Engine/Core/CommandLineManager.hpp`
- `src/Engine/Core/CoreTests.cpp`
- `docs/COMMAND_LINE_PARSER_README.md`
- `docs/COMMAND_LINE_PARSER_QUICK_REF.md`
- `docs/COMMAND_LINE_PARSER_DESIGN.md`
- `tools/validate_parser.py`

---

## Verification

```bash
cmake --preset=<preset>
cmake --build out/build/<preset> --target LudusTests
```

Expected: all `CommandLineManager_*` tests pass.
