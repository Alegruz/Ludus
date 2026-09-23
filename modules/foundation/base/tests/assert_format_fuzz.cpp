#include "internal/diagnostic_format.hpp"

using namespace ludus::foundation;
using namespace ludus::foundation::diagnostics;

// libFuzzer ABI spelling is prescribed by the toolchain.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const uint8* data, usize size)
{
    char output[2048];
    internal::TextWriter writer(output, sizeof(output));
    const detail::DiagnosticArg args[] = {
        detail::MakeDiagnosticArg(int64{-1}),
        detail::MakeDiagnosticArg(float64{-0.0}),
        detail::MakeDiagnosticArg(DiagnosticText{reinterpret_cast<const char*>(data), size}),
        detail::MakeDiagnosticArg(DiagnosticAddress(data))};
    internal::FormatDiagnostic(writer, {reinterpret_cast<const char*>(data), size}, args, size % 5);
    (void)writer.Finish();
    return 0;
}
