#pragma once

#include "diagnostic_text.hpp"
#include <ludus/foundation/base/assert_format.hpp>

namespace ludus::foundation::diagnostics::internal
{
// Includes the fixed array's final NUL in Format.Size. All memory must be valid;
// semantic invalidity never invokes assertions. Used by the bounded-memory fuzzer.
void FormatDiagnostic(TextWriter& writer,
                      DiagnosticText format,
                      const detail::DiagnosticArg* args,
                      usize count) noexcept;
} // namespace ludus::foundation::diagnostics::internal
