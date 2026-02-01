# Command Line Argument Pair Handling

## Overview

The command line argument system has been enhanced to properly handle paired arguments (flag + value) such as `--width 1024` and `--height 768`.

## Changes Made

### 1. CommandLineManager Enhancements

Added three new methods to `CommandLineManager` for indexed access and lookahead:

- **`TryGetArgument(size_t index, BasicString<CharT>& outArg)`**
  - Safely retrieves an argument at a specific index
  - Returns `true` if the argument exists, `false` otherwise
  - Example: `commandLine.TryGetArgument(0, arg)` gets the first argument

- **`TryGetArgumentPair(size_t index, BasicString<CharT>& outKey, BasicString<CharT>& outValue)`**
  - Retrieves both a flag and its value simultaneously
  - Returns `true` if both index and index+1 exist
  - Useful for efficient key-value pair retrieval

- **`GetArgumentCount()`**
  - Returns total number of arguments
  - Essential for safe iteration without out-of-bounds access

### 2. WindowManager Parsing Improvements

#### Legacy Method (Still Supported)
The original `HandleArgument()` method remains unchanged for backward compatibility:
```cpp
void HandleArgument(const BasicString<CharT>& argument) noexcept;
```

#### New Context-Aware Method
Added `ParseCommandLineWithContext()` which:
- Accepts the full `CommandLineManager` for indexed access
- Properly handles paired arguments with values
- Skips the value argument after consuming it to avoid reprocessing
- Safely parses numeric values with validation

```cpp
template<core::StringCharType CharT>
void ParseCommandLineWithContext(const core::CommandLineManager<CharT>& commandLine) noexcept;
```

## Usage Example

### Before (Limited)
```cpp
// Could only process individual flags, not their values
commandLine.ParseCommandLine(windowManager);
// Result: --width and 1024 processed separately, width ignored
```

### After (Full Support)
```cpp
// Process paired arguments properly
windowManager.ParseCommandLineWithContext(commandLine);

// Command line: --width 1024 --height 768
// Result: Window dimensions set correctly to 1024x768
```

## Implementation Details

The new parsing logic:

1. **Iterates through all arguments by index**
   ```cpp
   for (size_t i = 0; i < argCount; ++i)
   ```

2. **Looks ahead for value arguments**
   ```cpp
   if (commandLine.TryGetArgument(i + 1, valueArg)) { ... }
   ```

3. **Validates and converts values**
   ```cpp
   if (core::StringToInt32(valueStr.GetCStr(), width) && width > 0)
   ```

4. **Skips processed value arguments**
   ```cpp
   ++i; // Move past the value we just consumed
   ```

5. **Handles both narrow and wide characters**
   ```cpp
   if constexpr (std::is_same_v<CharT, wchar_t>) { ... }
   ```

## Supported Window Arguments

| Flag | Short | Format | Example |
|------|-------|--------|---------|
| `--width` | `-w` | `-w VALUE` or `--width VALUE` | `--width 1920` |
| `--height` | `-h` | `-h VALUE` or `--height VALUE` | `--height 1080` |

## Backward Compatibility

The system maintains full backward compatibility:
- Old code using `ParseCommandLine()` and `HandleArgument()` continues to work
- New code can use `ParseCommandLineWithContext()` for paired argument support
- Both methods can be used in the same application if needed

## Benefits

1. **Proper Paired Argument Handling** - Flags and values are now correctly associated
2. **Type Safety** - Optional return types prevent out-of-bounds access
3. **Efficiency** - No redundant string parsing or iteration
4. **Extensibility** - Easy to add new paired arguments following the pattern
5. **Character Type Support** - Works with both narrow (`char`) and wide (`wchar_t`) strings
6. **Validation** - Numeric parsing includes bounds checking

## Future Enhancements

Potential improvements for future versions:
- Generic key-value pair parsing for non-numeric values
- Argument validation and requirement checking
- Help text generation from argument metadata
- Configuration file integration with command-line overrides
