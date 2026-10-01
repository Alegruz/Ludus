#pragma once

// Test-only helpers for loading the packaged fixture fonts without any
// system-font or network assumption. LUDUS_TEXT_FIXTURE_DIR is injected by the
// test target's CMake definition and points at tests/fixtures.

#include <ludus/foundation/base/types.h>

#include <cstdio>
#include <string>
#include <vector>

namespace ludus::text::test
{
using ludus::foundation::uint8;

inline std::vector<uint8> LoadFixtureFont(const char* fileName)
{
    std::string path = std::string(LUDUS_TEXT_FIXTURE_DIR) + "/" + fileName;
    std::vector<uint8> bytes;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
    {
        return bytes;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size > 0)
    {
        bytes.resize(static_cast<std::size_t>(size));
        const std::size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
        bytes.resize(read);
    }
    std::fclose(file);
    return bytes;
}

inline const char* LatinFont()
{
    return "NotoSans-Regular.ttf";
}
inline const char* HangulFont()
{
    return "NotoSansKR-Subset-Regular.ttf";
}
inline const char* ArabicFont()
{
    return "NotoSansArabic-Regular.ttf";
}
} // namespace ludus::text::test
