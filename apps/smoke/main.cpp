#include <ludus/foundation/base/version.hpp>

#include <iostream>

int main()
{
    std::cout << "Ludus " << ludus::foundation::version_string() << '\n';
    std::cout << "Revision: " << ludus::foundation::git_revision() << '\n';
    std::cout << "Compiler: " << ludus::foundation::compiler_identity() << '\n';

    return 0;
}
