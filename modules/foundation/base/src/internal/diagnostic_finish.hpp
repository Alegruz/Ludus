#pragma once
#include <ludus/foundation/base/assert.hpp>

namespace ludus::foundation::diagnostics::detail
{
// Private bridge: bytes have already been escaped by the closed formatter.
// Keeping the dependency in this direction leaves plain-only binaries free of
// the parser and floating conversion. Begin has already established ownership.
[[noreturn]] void FinishFatalRendered(DiagnosticText message) noexcept;
bool FinishCheckRendered(DiagnosticText message) noexcept;
} // namespace ludus::foundation::diagnostics::detail
