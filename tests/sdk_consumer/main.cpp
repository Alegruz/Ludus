#include <ludus/foundation/base/build_metadata.hpp>
#include <ludus/foundation/base/version.hpp>

#include <iostream>

int main()
{
    const auto value = ludus::foundation::version();
    if (value.major != ludus::foundation::build_metadata::version_major) {
        return 1;
    }

    std::cout << "SDK consumer linked Ludus " << ludus::foundation::version_string() << '\n';
    return 0;
}
