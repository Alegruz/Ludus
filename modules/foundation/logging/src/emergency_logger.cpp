#include "internal/emergency_logger.hpp"

#include <ludus/foundation/base/diagnostic.hpp>

namespace ludus::foundation::logging::internal
{

namespace
{

// Map a logging severity onto the Base diagnostic vocabulary. The emergency path
// is only reached for Warning+ (and Fatal), plus internal notes.
[[nodiscard]] base::DiagnosticSeverity ToDiagnosticSeverity(LogLevel level) noexcept
{
    switch (level)
    {
        case LogLevel::Trace:
        case LogLevel::Debug:
        case LogLevel::Info:
            return base::DiagnosticSeverity::Note;
        case LogLevel::Warning:
            return base::DiagnosticSeverity::Warning;
        case LogLevel::Error:
            return base::DiagnosticSeverity::Error;
        case LogLevel::Fatal:
            return base::DiagnosticSeverity::Fatal;
    }
    return base::DiagnosticSeverity::Warning;
}

} // namespace

void EmergencyLog(LogLevel level,
                  LogCategory category,
                  std::string_view message,
                  const std::source_location& location) noexcept
{
    // Thin adapter over the FoundationBase primitive. Logging owns no independent
    // emergency implementation any more: the reporter lives below Logging so
    // Base and the (proposed) assertion subsystem share it without depending
    // upward on Logging (requirements R5, R31).
    base::EmergencyReport(ToDiagnosticSeverity(level), category.Name, message, location);
}

void EmergencyNote(std::string_view message, const std::source_location& location) noexcept
{
    base::EmergencyReport(base::DiagnosticSeverity::Warning, "Logging", message, location);
}

} // namespace ludus::foundation::logging::internal
