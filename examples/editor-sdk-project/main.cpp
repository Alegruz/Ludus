// Minimal external Ludus SDK consumer launched by the editor workspace.
//
// This project depends ONLY on the installed Ludus SDK through public headers
// and exported CMake targets; it compiles no engine sources of its own. It is
// the "external installed-SDK sample" exercised by editor requirement E13.

#include <ludus/foundation/base/version.hpp>

#include <cstdio>

int main(int argc, char** argv)
{
    const auto version = ludus::foundation::version();
    std::fprintf(stdout, "External SDK sample using Ludus %u.%u.%u\n",
                 static_cast<unsigned>(version.major),
                 static_cast<unsigned>(version.minor),
                 static_cast<unsigned>(version.patch));
    // Echo any run arguments so the editor's exact-argv preservation can be
    // observed through this external consumer as well.
    for (int i = 1; i < argc; ++i)
    {
        std::fprintf(stdout, "arg[%d]=%s\n", i, argv[i]);
    }
    std::fflush(stdout);
    return 0;
}
