#include "internal/formatter.hpp"

#include <ludus/foundation/logging/log.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

namespace ludus::foundation::logging::internal {

namespace {

// ANSI SGR color codes keyed by level (spec section 19). Trace is dim; Debug and
// Info are default; Warning yellow; Error red; Fatal bright red.
[[nodiscard]] std::string_view color_for(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Trace:
            return "\x1b[2m";
        case LogLevel::Debug:
        case LogLevel::Info:
            return "";
        case LogLevel::Warning:
            return "\x1b[33m";
        case LogLevel::Error:
            return "\x1b[31m";
        case LogLevel::Fatal:
            return "\x1b[91m";
    }
    return "";
}

constexpr std::string_view color_reset = "\x1b[0m";

// Monotonic thread-id allocator. Not the OS tid: a small, stable, readable
// counter so log output is deterministic across runs for a given thread order.
std::atomic<std::uint32_t> g_next_thread_id{0};

struct ThreadLocalIdentity
{
    std::uint32_t id;
    std::string name;

    ThreadLocalIdentity()
        : id(g_next_thread_id.fetch_add(1, std::memory_order_relaxed)),
          name(id == 0 ? std::string{"Main"} : "Thread-" + std::to_string(id))
    {
    }
};

ThreadLocalIdentity& identity() noexcept
{
    thread_local ThreadLocalIdentity value;
    return value;
}

} // namespace

bool source_visible_for(LogLevel level) noexcept
{
    return level >= LogLevel::Warning;
}

std::size_t format_timestamp(std::uint64_t timestamp_ns, char* out, std::size_t capacity) noexcept
{
    if (capacity < 13) {
        if (capacity > 0) {
            out[0] = '\0';
        }
        return 0;
    }

    const std::uint64_t total_seconds = timestamp_ns / 1'000'000'000ull;
    const std::uint32_t milliseconds = static_cast<std::uint32_t>((timestamp_ns / 1'000'000ull) % 1000ull);

    const std::time_t seconds = static_cast<std::time_t>(total_seconds);
    std::tm broken{};
#if defined(_WIN32)
    localtime_s(&broken, &seconds);
#else
    localtime_r(&seconds, &broken);
#endif

    const int written =
        std::snprintf(out, capacity, "%02d:%02d:%02d.%03u", broken.tm_hour, broken.tm_min, broken.tm_sec, milliseconds);
    return written > 0 ? static_cast<std::size_t>(written) : 0;
}

void format_console_line(const LogRecordView& record, bool use_color, std::string& buffer)
{
    buffer.clear();

    char timestamp[16];
    const std::size_t ts_len = format_timestamp(record.timestamp_ns, timestamp, sizeof(timestamp));
    buffer.append(timestamp, ts_len);

    buffer.append(" [");
    buffer.append(record.thread_name.empty() ? std::string_view{"?"} : record.thread_name);
    buffer.append("] [");

    if (use_color) {
        buffer.append(color_for(record.level));
    }
    buffer.append(to_padded_string(record.level));
    if (use_color) {
        buffer.append(color_reset);
    }

    buffer.append("] [");
    buffer.append(record.category.name);
    buffer.append("] ");
    buffer.append(record.message);

    if (source_visible_for(record.level) && !record.file.empty()) {
        buffer.append("\n    ");
        buffer.append(record.file);
        buffer.push_back(':');
        buffer.append(std::to_string(record.line));
    }
}

std::string_view current_thread_name() noexcept
{
    return identity().name;
}

std::uint32_t current_thread_id() noexcept
{
    return identity().id;
}

std::uint64_t now_nanoseconds() noexcept
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

} // namespace ludus::foundation::logging::internal

namespace ludus::foundation::logging {

void set_current_thread_name(std::string_view name)
{
    internal::identity().name.assign(name);
}

} // namespace ludus::foundation::logging
