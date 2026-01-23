# Command-Line Parser Quick Reference

## Quick Examples

### Basic Usage (No Changes)
```cpp
// Simple space-separated arguments
CommandLineManager<char> manager = 
    CommandLineManager<char>::Create("program arg1 arg2 arg3");
// Result: ["program", "arg1", "arg2", "arg3"]
```

### With Spaces in Arguments
```cpp
// Using quotes
CommandLineManager<char> manager = 
    CommandLineManager<char>::Create("program \"hello world\" arg2");
// Result: ["program", "hello world", "arg2"]
```

### Escape Sequences
```cpp
// Windows paths
CommandLineManager<wchar_t> manager = 
    CommandLineManager<wchar_t>::Create(
        L"copy \"C:\\Program Files\\src.txt\" \"C:\\Program Files\\dst.txt\"");
// Result: ["copy", "C:\\Program Files\\src.txt", "C:\\Program Files\\dst.txt"]

// Multi-line arguments
CommandLineManager<char> manager = 
    CommandLineManager<char>::Create("cmd \"line1\\nline2\\nline3\"");
// Result: ["cmd", "line1\nline2\nline3"]
```

## Escape Sequence Reference

| Input | Output | Use Case |
|-------|--------|----------|
| `\\` | `\` | Backslash |
| `\"` | `"` | Quote inside quotes |
| `\'` | `'` | Apostrophe inside quotes |
| `\n` | newline | Multi-line values |
| `\t` | tab | Tabs in arguments |
| `\r` | CR | Carriage return |
| `\ ` | space | Space outside quotes |

## Common Patterns

### File Path
```
Input:  "C:\\Users\\name\\Documents\\file.txt"
Result: C:\Users\name\Documents\file.txt
```

### Message with Quotes
```
Input:  "msg \"Hello, World!\""
Result: msg "Hello, World!"
```

### Configuration Key=Value
```
Input:  --config="key1=value1" --config="key2=value with spaces"
Result: ["--config=key1=value1", "--config=key2=value with spaces"]
```

### Directory with Mixed Separators
```
Input:  "/usr/local/bin/../config\\file.ini"
Result: /usr/local/bin/../config\file.ini
```

## Implementation Location

- **Header**: [include/Ludus/Engine/Core/CommandLineManager.hpp](../../include/Ludus/Engine/Core/CommandLineManager.hpp) - `Create(CharT* commandLine)` method
- **Implementation**: Same file (template implementation)
- **Tests**: [src/Engine/Core/CoreTests.cpp](../../src/Engine/Core/CoreTests.cpp) - `CommandLineManager_*` tests

## API Reference

### Creating Manager from String
```cpp
template<StringCharType CharT>
CommandLineManager<CharT> CommandLineManager<CharT>::Create(CharT* commandLine) noexcept
```

**Parameters:**
- `commandLine`: Null-terminated string to parse

**Returns:** `CommandLineManager` instance with parsed arguments

**Supported character types:** `char`, `wchar_t`

### Getting Parsed Arguments
```cpp
const DynamicArray<BasicString<CharT>>& GetArguments() const noexcept
```

**Returns:** Reference to array of parsed argument strings

## Performance

- **Time Complexity**: O(n) - single pass through input
- **Space Complexity**: O(n) - for storing output arguments
- **No allocations**: Beyond final argument storage

## Notes

- Empty quoted strings produce empty arguments: `"" → [empty string]`
- Unclosed quotes consume rest of line
- Multiple spaces treated as single separator
- Unknown escapes preserve backslash: `\x` → `\x`
- Fully backward compatible with space-only parsing

## Testing

Run validation:
```bash
python tools/validate_parser.py
```

Expected output:
```
Results: 12 passed, 0 failed
✓ All tests passed!
```
