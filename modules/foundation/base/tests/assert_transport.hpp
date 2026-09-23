#pragma once

#include <ludus/foundation/base/diagnostic_output.hpp>

#include <cstdlib>

inline void ConfigureTestTransport()
{
    const char* descriptor = std::getenv("LUDUS_TEST_DIAGNOSTIC_FD");
    if (descriptor != nullptr && !ludus::foundation::diagnostics::ConfigureEmergencySocket(std::atoi(descriptor)))
    {
        std::_Exit(79);
    }
}
