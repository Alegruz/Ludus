#!/usr/bin/env python3
"""
Command-line parser validation script.

This script simulates the CommandLineManager parsing logic in Python
to validate that the implementation works correctly without requiring a full build.
"""

def parse_command_line(cmd_line: str) -> list[str]:
    """
    Parse a command line string into arguments, supporting quotes and escape sequences.
    
    This mimics the C++ CommandLineManager::Create(CharT* commandLine) implementation.
    
    Args:
        cmd_line: The command line string to parse
        
    Returns:
        List of parsed arguments
    """
    arguments = []
    current_argument = []
    i = 0
    in_quotes = False
    quote_char = None
    just_closed_quotes = False
    
    while i < len(cmd_line):
        # Handle escape sequences
        if cmd_line[i] == '\\' and i + 1 < len(cmd_line):
            next_char = cmd_line[i + 1]
            
            # Recognize common escape sequences
            if next_char == 'n':
                current_argument.append('\n')
                i += 2
            elif next_char == 't':
                current_argument.append('\t')
                i += 2
            elif next_char == 'r':
                current_argument.append('\r')
                i += 2
            elif next_char == '\\':
                current_argument.append('\\')
                i += 2
            elif next_char == '"':
                current_argument.append('"')
                i += 2
            elif next_char == "'":
                current_argument.append("'")
                i += 2
            elif next_char == ' ':
                current_argument.append(' ')
                i += 2
            else:
                # Unknown escape sequence - keep the backslash
                current_argument.append('\\')
                i += 1
            just_closed_quotes = False
            continue
        
        # Handle quote characters
        if (cmd_line[i] == '"' or cmd_line[i] == "'") and not in_quotes:
            in_quotes = True
            quote_char = cmd_line[i]
            just_closed_quotes = False
            i += 1
            continue
        
        if cmd_line[i] == quote_char and in_quotes:
            in_quotes = False
            quote_char = None
            just_closed_quotes = True
            i += 1
            continue
        
        # Handle spaces (argument separator if not in quotes)
        if cmd_line[i] == ' ' and not in_quotes:
            # Skip consecutive spaces
            while i < len(cmd_line) and cmd_line[i] == ' ':
                i += 1
            
            # Add the argument if we have one or if we just closed quotes (even if empty)
            if current_argument or just_closed_quotes:
                arguments.append(''.join(current_argument))
                current_argument = []
                just_closed_quotes = False
            continue
        
        # Regular character
        current_argument.append(cmd_line[i])
        just_closed_quotes = False
        i += 1
    
    # Add the final argument if we have one or if we just closed quotes
    if current_argument or just_closed_quotes:
        arguments.append(''.join(current_argument))
    
    return arguments


def test_case(name: str, cmd_line: str, expected: list[str]) -> bool:
    """Test a single parsing case."""
    result = parse_command_line(cmd_line)
    passed = result == expected
    status = "PASS" if passed else "FAIL"
    print(f"[{status}] {name}")
    if not passed:
        print(f"  Input:    {cmd_line!r}")
        print(f"  Expected: {expected}")
        print(f"  Got:      {result}")
    return passed


def main():
    """Run all test cases."""
    print("Command-Line Parser Validation Tests")
    print("=" * 50)
    
    tests = [
        ("Basic parsing", 
         "program arg1 arg2 arg3",
         ["program", "arg1", "arg2", "arg3"]),
        
        ("Double quotes",
         'program "hello world" arg2',
         ["program", "hello world", "arg2"]),
        
        ("Single quotes",
         "program 'single quoted' arg2",
         ["program", "single quoted", "arg2"]),
        
        ("Escape sequences",
         'program "hello\\nworld" "tab\\there"',
         ["program", "hello\nworld", "tab\there"]),
        
        ("Escape quotes",
         'program "say \\"hello\\"" \'don\\\'t\'',
         ["program", 'say "hello"', "don't"]),
        
        ("Escape space",
         "program hello\\ world arg2",
         ["program", "hello world", "arg2"]),
        
        ("Mixed quotes and escapes",
         'program "path\\nwith spaces" unquoted\\ value \'another arg\'',
         ["program", "path\nwith spaces", "unquoted value", "another arg"]),
        
        ("Multiple spaces",
         "program    arg1     arg2",
         ["program", "arg1", "arg2"]),
        
        ("Empty quotes",
         'program "" arg2',
         ["program", "", "arg2"]),
        
        ("Backslash at end",
         "program test\\\\",
         ["program", "test\\"]),
        
        ("Complex Windows path",
         'program "C:\\Program Files\\App\\config.ini"',
         ["program", "C:\\Program Files\\App\\config.ini"]),
        
        ("File operations",
         'copy "source file.txt" "destination folder\\file.txt"',
         ["copy", "source file.txt", "destination folder\\file.txt"]),
    ]
    
    passed = 0
    failed = 0
    
    for name, cmd_line, expected in tests:
        if test_case(name, cmd_line, expected):
            passed += 1
        else:
            failed += 1
    
    print("=" * 50)
    print(f"Results: {passed} passed, {failed} failed")
    
    if failed == 0:
        print("All tests passed!")
        return 0
    else:
        print("Some tests failed")
        return 1


if __name__ == "__main__":
    exit(main())
