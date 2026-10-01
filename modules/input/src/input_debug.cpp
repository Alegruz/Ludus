#include <ludus/input/input_debug.h>

namespace ludus::input
{
void InputDebugTrace::Record(const TraceEntry& entry) noexcept
{
    ++mTotal;
    if (entry.Kind == TraceKind::Overflow)
    {
        mSawOverflow = true;
    }
    if (mSize == TRACE_CAPACITY)
    {
        // Ring is full: overwrite the oldest and mark truncation (trace loss,
        // tracked separately from gameplay overflow).
        mWrapped = true;
        ++mTruncated;
        mEntries[mHead] = entry;
        mHead = (mHead + 1) % TRACE_CAPACITY;
        return;
    }
    mEntries[mHead] = entry;
    mHead = (mHead + 1) % TRACE_CAPACITY;
    ++mSize;
}

std::span<const TraceEntry> InputDebugTrace::View() const noexcept
{
    // Linearize the ring into mLinear in chronological order. The oldest valid
    // entry is at (mHead - mSize) modulo capacity.
    const usize start = (mHead + TRACE_CAPACITY - mSize) % TRACE_CAPACITY;
    for (usize i = 0; i < mSize; ++i)
    {
        mLinear[i] = mEntries[(start + i) % TRACE_CAPACITY];
    }
    return std::span<const TraceEntry>(mLinear, mSize);
}

void InputDebugTrace::Clear() noexcept
{
    mHead = 0;
    mSize = 0;
    mWrapped = false;
    mSawOverflow = false;
    mTruncated = 0;
    mTotal = 0;
}
} // namespace ludus::input
