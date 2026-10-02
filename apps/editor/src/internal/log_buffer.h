#pragma once

// LogBuffer: bounded retained output with explicit, visible truncation.
//
// Private editor header (not installed). Holds at most a fixed number of UTF-8
// bytes and text blocks of streamed child output, counting and surfacing
// omissions rather than growing without bound. It never stores the last-result
// information (that lives in WorkspaceState) so logs cannot overwrite it.
// See design.md section 10.

#include <ludus/foundation/base/types.h>

#include <QString>
#include <QStringList>

namespace ludus::editor
{
using foundation::uint64;
using foundation::usize;

enum class OutputStream : foundation::uint8
{
    Stdout,
    Stderr,
};

class LogBuffer
{
public:
    // Conservative initial bounds (design section 10); measured in E0.5.
    static constexpr usize MaxBytes = 1u * 1024u * 1024u;   // <= 1 MiB UTF-8
    static constexpr usize MaxBlocks = 5000u;               // <= 5000 text blocks

    LogBuffer() = default;

    // Append a chunk of already-decoded plain text for a stream. Chunks need not
    // be complete lines. When adding would exceed a bound, the oldest blocks are
    // dropped and the dropped count is incremented (visible truncation).
    void Append(OutputStream stream, const QString& text);

    // Drop everything and reset counts (Clear Output). Does not touch results.
    void Clear();

    // The retained text as a list of blocks (oldest first), each tagged with its
    // stream only implicitly by content; callers that need per-stream separation
    // can keep two buffers. Here output is interleaved in arrival order.
    [[nodiscard]] const QStringList& Blocks() const noexcept { return Blocks_; }

    // Combined retained text, with a leading omission marker when output was
    // dropped so truncation is always visible in what the user copies/reads.
    [[nodiscard]] QString Text() const;

    [[nodiscard]] uint64 DroppedBlocks() const noexcept { return DroppedBlocks_; }
    [[nodiscard]] uint64 DroppedBytes() const noexcept { return DroppedBytes_; }
    [[nodiscard]] usize RetainedBytes() const noexcept { return RetainedBytes_; }

private:
    void EnforceBounds();

    QStringList Blocks_;
    usize RetainedBytes_ = 0;
    uint64 DroppedBlocks_ = 0;
    uint64 DroppedBytes_ = 0;
};

} // namespace ludus::editor
