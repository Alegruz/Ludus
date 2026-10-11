"""Keep pinned Catch2's ABI, but erase stream insertion from test headers.

Generated copies retain upstream licensing. No dependency-cache files change.
Thanks to Catch2 Authors, Catch2 3.4.0 catch_tostring/catch_decomposer and
catch_reusable_string_stream (https://github.com/catchorg/Catch2/tree/v3.4.0/src/catch2).
We preserve assertion evaluation and scalar/range printers; chrono printers
become explicit opt-in and stream operations compile once in catch_stream.cpp.
"""
import argparse
from pathlib import Path


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"Pinned Catch2 header changed: expected one occurrence of {old!r}")
    return text.replace(old, new)


def prepare(source, destination):
    files = {
        "internal/catch_reusable_string_stream.hpp": source / "internal/catch_reusable_string_stream.hpp",
        "internal/catch_decomposer.hpp": source / "internal/catch_decomposer.hpp",
        "catch_tostring.hpp": source / "catch_tostring.hpp",
    }
    contents = {name: path.read_text(encoding="utf-8") for name, path in files.items()}
    stream = contents["internal/catch_reusable_string_stream.hpp"]
    stream = replace_once(stream, "#include <ostream>", '#include "catch_stream.hpp"')
    stream = replace_once(stream, "*m_oss << value;", "ludus::test::Insert(*m_oss, value);")
    contents["internal/catch_reusable_string_stream.hpp"] = stream
    contents["internal/catch_decomposer.hpp"] = replace_once(
        contents["internal/catch_decomposer.hpp"],
        "os << Catch::Detail::stringify( m_lhs );",
        "ludus::test::Insert(os, Catch::Detail::stringify( m_lhs ));",
    )
    stringify = contents["catch_tostring.hpp"]
    first = stringify.index("// Separate std::chrono::duration specialization")
    last = stringify.index("#include <catch2/interfaces/catch_interfaces_registry_hub.hpp>", first)
    chrono = stringify[first:last]
    # Preserve the upstream chrono contract for the few tests that need it.
    contents["catch_chrono_stringmaker.hpp"] = (
        stringify[:stringify.index("#ifndef CATCH_TOSTRING_HPP_INCLUDED")]
        + '#pragma once\n#include <catch2/catch_tostring.hpp>\n' + chrono
    )
    contents["catch_tostring.hpp"] = stringify[:first] + """
// Long double's default stream printer is erased too; its availability must not
// depend on whether a consumer explicitly included <ostream>.
namespace Catch {
    template<> struct StringMaker<long double> {
        static std::string convert(long double value);
    };
}
""" + stringify[last:]
    for name, content in contents.items():
        target = destination / "catch2" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_text(encoding="utf-8") != content:
            target.write_text(content, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    prepare(args.source, args.destination)
