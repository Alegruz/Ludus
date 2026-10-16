#pragma once

// Test-only helpers for loading the packaged fixture fonts without any
// system-font or network assumption. LUDUS_TEXT_FIXTURE_DIR is injected by the
// test target's CMake definition and points at tests/fixtures.

#include <ludus/foundation/base/types.h>
#include <ludus/foundation/containers/array.hpp>

#include <cstdio>
#include <string>

namespace ludus::text::test
{
using ludus::foundation::Array;
using ludus::foundation::uint8;
using ludus::foundation::usize;

inline Array<uint8> LoadFixtureFont(const char* fileName)
{
    std::string path = std::string(LUDUS_TEXT_FIXTURE_DIR) + "/" + fileName;
    Array<uint8> bytes;
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
        bytes.Resize(static_cast<usize>(size));
        const usize read = std::fread(bytes.GetData(), 1, bytes.GetSize(), file);
        bytes.Resize(read);
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
