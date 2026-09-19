#include "internal/emergency_logger.hpp"

#include <array>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char*);
extern "C" __declspec(dllimport) int __stdcall IsDebuggerPresent(void);
#endif

namespace ludus::foundation::logging::internal
{

namespace
{

// Append a NUL-terminated C string to a fixed buffer, never overflowing. Updates
// `length` and always leaves the buffer NUL-terminated. Allocation-free.
void append(std::array<char, 512>& buffer, std::size_t& length, std::string_view text) noexcept
{
    const std::size_t capacity = buffer.size() - 1; // reserve space for terminator
    const std::size_t remaining = capacity - length;
    const std::size_t count = text.size() < remaining ? text.size() : remaining;
    if (count > 0)
    {
        std::memcpy(buffer.data() + length, text.data(), count);
        length += count;
    }
    buffer[length] = '\0';
}

} // namespace

void EmergencyLog(LogLevel level,
                  LogCategory category,
                  std::string_view message,
                  const std::source_location& location) noexcept
{
    // Build a single line in a fixed stack buffer, then issue one write. The
    // format is intentionally minimal and dependency-free:
    //   [LUDUS:LEVEL] [Category] message (file:line)
    std::array<char, 512> buffer{};
    std::size_t length = 0;

    append(buffer, length, "[LUDUS:");
    append(buffer, length, ToString(level));
    append(buffer, length, "] [");
    append(buffer, length, category.Name);
    append(buffer, length, "] ");
    append(buffer, length, message);

    append(buffer, length, " (");
    append(buffer, length, location.file_name());
    append(buffer, length, ":");
    // Format the line number without heap allocation.
    std::array<char, 16> line_digits{};
    const int written =
        std::snprintf(line_digits.data(), line_digits.size(), "%u", static_cast<unsigned>(location.line()));
    if (written > 0)
    {
        append(buffer, length, std::string_view(line_digits.data(), static_cast<std::size_t>(written)));
    }
    append(buffer, length, ")\n");

    // Direct, unbuffered write to stderr. std::fwrite to stderr does not throw
    // and does not depend on the logging backend.
    std::fwrite(buffer.data(), 1, length, stderr);
    std::fflush(stderr);

#if defined(_WIN32)
    if (IsDebuggerPresent())
    {
        OutputDebugStringA(buffer.data());
    }
#endif
}

void EmergencyNote(std::string_view message, const std::source_location& location) noexcept
{
    EmergencyLog(LogLevel::Warning, LogCategory{0u, "Logging"}, message, location);
}

} // namespace ludus::foundation::logging::internal
