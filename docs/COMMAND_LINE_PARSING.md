# Command-Line Parsing Enhancement

## Overview

This document describes the improved command-line parsing implementation in `CommandLineManager`, which now supports quoted strings and escape sequences.

## Previous Behavior

The original implementation only performed simple space-splitting:
- Input: `program "hello world" unquoted`
- Parsed as: `["program", "\"hello", "world\"", "unquoted"]` ❌ Incorrect!

## New Behavior

The enhanced parser now supports:
1. **Double-quoted strings**: `"hello world"` → single argument with spaces
2. **Single-quoted strings**: `'hello world'` → single argument with spaces  
3. **Escape sequences**: Within quoted strings
4. **Escape characters**: Outside of quotes for handling spaces

## Supported Escape Sequences

Inside quoted strings, the following escape sequences are recognized:

| Escape Sequence | Result |
|-----------------|--------|
| `\"` | Double quote character |
| `\'` | Single quote character |
| `\\` | Backslash character |
| `\n` | Newline character |
| `\t` | Tab character |
| `\r` | Carriage return |
| `\ ` | Space character (for escaping spaces outside quotes) |

## Usage Examples

### Basic Parsing
```cpp
char cmdLine[] = "program arg1 arg2 arg3";
CommandLineManager<char> manager = CommandLineManager<char>::Create(cmdLine);
// Result: ["program", "arg1", "arg2", "arg3"]
```

### Double-Quoted Strings
```cpp
char cmdLine[] = "program \"hello world\" arg2";
CommandLineManager<char> manager = CommandLineManager<char>::Create(cmdLine);
// Result: ["program", "hello world", "arg2"]
```

### Single-Quoted Strings
```cpp
char cmdLine[] = "program 'single quoted' arg2";
CommandLineManager<char> manager = CommandLineManager<char>::Create(cmdLine);
// Result: ["program", "single quoted", "arg2"]
```

### Escape Sequences in Quotes
```cpp
char cmdLine[] = "program \"line1\\nline2\" \"tab\\there\"";
CommandLineManager<char> manager = CommandLineManager<char>::Create(cmdLine);
// Result: ["program", "line1\nline2", "tab\there"]
```

### Escaping Quotes
```cpp
char cmdLine[] = "program \"say \\\"hello\\\"\"";
CommandLineManager<char> manager = CommandLineManager<char>::Create(cmdLine);
// Result: ["program", "say \"hello\""]
```

### Space Escaping (Outside Quotes)
```cpp
char cmdLine[] = "program hello\\ world arg2";
CommandLineManager<char> manager = CommandLineManager<char>::Create(cmdLine);
// Result: ["program", "hello world", "arg2"]
```

### Mixed Usage
```cpp
char cmdLine[] = "program \"path with spaces\" unquoted\\ value 'another arg'";
CommandLineManager<char> manager = CommandLineManager<char>::Create(cmdLine);
// Result: ["program", "path with spaces", "unquoted value", "another arg"]
```

## Implementation Details

### `CommandLineManager::Create(CharT* commandLine)`

The parsing algorithm operates as a simple state machine with the following states:

1. **Normal parsing**: Reads characters until a space (outside quotes) is encountered
2. **Quoted mode**: When inside quotes, reads all characters including spaces
3. **Escape mode**: When a backslash is encountered, interprets the next character specially

Key features:
- **Quote handling**: Tracks whether we're inside quotes and which quote character opened them
- **Escape sequences**: Converts `\n`, `\t`, `\r`, `\\` and quote escapes
- **Multiple spaces**: Consecutive spaces are treated as a single separator
- **Empty arguments**: Quoted empty strings `""` or `''` produce empty arguments

### Algorithm Flow

```
while character is not null:
    if backslash and next char exists:
        handle escape sequence
    elif quote character and not in quotes:
        enter quote mode
    elif quote character matches current quote char and in quotes:
        exit quote mode
    elif space and not in quotes:
        skip consecutive spaces
        push current argument if not empty
    else:
        add character to current argument
```

## Backward Compatibility

The new implementation is **fully backward compatible**:
- Simple space-separated arguments work exactly as before
- No quotes or escapes = same behavior as original
- The C-style `Create(int argc, char** argv)` overload is unchanged

## Testing

Comprehensive unit tests are included in [src/Engine/Core/CoreTests.cpp](../src/Engine/Core/CoreTests.cpp):

- `CommandLineManager_BasicParsing` - Simple space-separated args
- `CommandLineManager_DoubleQuotes` - Double-quoted strings with spaces
- `CommandLineManager_SingleQuotes` - Single-quoted strings
- `CommandLineManager_EscapeSequences` - Newline, tab, carriage return escapes
- `CommandLineManager_EscapeQuotes` - Escaping quote characters
- `CommandLineManager_EscapeSpace` - Escaping spaces outside quotes
- `CommandLineManager_MixedQuotesAndEscapes` - Complex mixed usage
- `CommandLineManager_MultipleSpaces` - Consecutive space handling
- `CommandLineManager_EmptyQuotes` - Empty quoted strings
- `CommandLineManager_BackslashAtEnd` - Trailing backslash handling

## Limitations

- **Unclosed quotes**: If a quoted string is not properly closed, all remaining text is treated as part of that argument
- **Unknown escape sequences**: Unrecognized sequences (e.g., `\x`) preserve the backslash
- **Quote nesting**: Quote types cannot be nested (e.g., `"outer 'inner' outer"` needs careful escaping)

## Use Cases

This enhancement is particularly valuable for:

1. **File paths**: `"C:\Program Files\My App\config.ini"`
2. **Arguments with spaces**: `--message="Hello, World!"`
3. **Configuration loading**: Parsing config files with complex arguments
4. **Command wrapping**: Tools that re-invoke other programs with arguments
5. **User input**: UI prompts for command-line arguments

## Performance

The implementation has **O(n)** time complexity where n is the length of the input string, with a single pass through the entire command line. No additional string allocations occur during parsing.

## Future Enhancements

Potential improvements for future versions:

1. **Configurable escape character**: Allow alternative to backslash
2. **Quote types**: Extend to support backticks or other quote styles
3. **Environment variable expansion**: `${VAR}` or `$VAR` substitution
4. **Error reporting**: Detailed feedback for malformed command lines
5. **Shell-like globbing**: Pattern expansion for file arguments
