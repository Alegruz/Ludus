#!/bin/bash

echo "========================================"
echo "Ludus Project Initialization"
echo "========================================"
echo

echo
echo "========================================"
echo "Initialization Complete!"
echo "========================================"
echo

# Check and install LLVM tools (clang-tidy, clang-format)
echo "Checking for LLVM tools (clang-tidy, clang-format)..."
if ! command -v clang-tidy &> /dev/null; then
    echo "clang-tidy not found. Attempting installation..."
    
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        # Linux (Debian/Ubuntu)
        if command -v apt-get &> /dev/null; then
            echo "Installing via apt-get..."
            sudo apt-get update
            sudo apt-get install -y clang-tidy clang-format
        else
            echo "apt-get not found. Install manually: sudo apt-get install clang-tidy clang-format"
        fi
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS
        if command -v brew &> /dev/null; then
            echo "Installing via Homebrew..."
            brew install llvm
        else
            echo "Homebrew not found. Install from https://brew.sh"
            echo "Then run: brew install llvm"
        fi
    fi
else
    echo "clang-tidy found!"
fi

echo
echo "========================================"
echo "Setup Complete!"
echo "========================================"
