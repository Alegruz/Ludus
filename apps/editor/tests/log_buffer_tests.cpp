// Offscreen tests for LogBuffer bounded retention and visible truncation
// (design 10; E11).

#include "internal/log_buffer.h"

#include <catch2/catch_test_macros.hpp>

#include <QString>

using namespace ludus::editor;

TEST_CASE("Retained bytes stay within the byte bound", "[editor][log]")
{
    LogBuffer buffer;
    const QString block(1024, QLatin1Char('x')); // 1 KiB per block
    for (int i = 0; i < 4000; ++i)
    {
        buffer.Append(OutputStream::Stdout, block);
    }
    CHECK(buffer.RetainedBytes() <= LogBuffer::MaxBytes);
    CHECK(buffer.DroppedBlocks() > 0);
    // The visible text begins with an omission marker when output was dropped.
    CHECK(buffer.Text().startsWith(QStringLiteral("[...")));
}

TEST_CASE("A single giant line without newline is truncated, not unbounded", "[editor][log]")
{
    LogBuffer buffer;
    const QString giant(static_cast<ludus::foundation::isize>(LogBuffer::MaxBytes) * 2, QLatin1Char('y'));
    buffer.Append(OutputStream::Stdout, giant);
    CHECK(buffer.RetainedBytes() <= LogBuffer::MaxBytes);
    CHECK(buffer.DroppedBytes() > 0);
}

TEST_CASE("Block count bound is enforced", "[editor][log]")
{
    LogBuffer buffer;
    for (int i = 0; i < static_cast<int>(LogBuffer::MaxBlocks) + 100; ++i)
    {
        buffer.Append(OutputStream::Stdout, QStringLiteral("a"));
    }
    CHECK(static_cast<ludus::foundation::usize>(buffer.Blocks().size()) <= LogBuffer::MaxBlocks);
    CHECK(buffer.DroppedBlocks() >= 100);
}

TEST_CASE("Clear resets content and counters", "[editor][log]")
{
    LogBuffer buffer;
    buffer.Append(OutputStream::Stderr, QStringLiteral("hello"));
    buffer.Clear();
    CHECK(buffer.Blocks().isEmpty());
    CHECK(buffer.RetainedBytes() == 0);
    CHECK(buffer.DroppedBlocks() == 0);
}
