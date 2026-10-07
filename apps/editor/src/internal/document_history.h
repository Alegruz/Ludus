#pragma once

#include <ludus/foundation/base/types.h>

#include <optional>

// Thanks to Robert Nystrom, Game Programming Patterns (2014), "Command",
// "Undo and Redo": reversible before/after operations. This private Qt-free
// history adds bounded storage, checked heads and monotonic revisions; saved
// content remains owned by the document. No filesystem effect is an Undo item.
// https://gameprogrammingpatterns.com/command.html
// See docs/architecture/editor-gui-systems.md, Documents and edit transactions.
namespace ludus::editor
{
template <typename T, foundation::usize CAPACITY = 64>
class DocumentHistory
{
    static_assert(CAPACITY > 0);

public:
    // T supplies equality and exception-free value copying. History storage
    // itself allocates nothing; payload ownership belongs to T's adapter.
    [[nodiscard]] bool Commit(const T& before, const T& after, bool merge = false) noexcept
    {
        if (before == after || Revision_ == ~foundation::uint64{0})
        {
            return false;
        }
        if (merge && MergeAllowed_ && Cursor_ == Count_ && Cursor_ != 0 && Entries_[Cursor_ - 1]->After == before)
        {
            auto& entry = Entries_[Cursor_ - 1];
            entry->After = after;
            if (entry->Before == after)
            {
                entry.reset();
                --Cursor_;
                --Count_;
            }
        }
        else
        {
            for (auto index = Cursor_; index < Count_; ++index)
            {
                Entries_[index].reset();
            }
            Count_ = Cursor_;
            if (Count_ == CAPACITY)
            {
                for (foundation::usize index = 1; index < CAPACITY; ++index)
                {
                    Entries_[index - 1] = Entries_[index];
                }
                --Count_;
                --Cursor_;
            }
            Entries_[Count_++] = Entry{before, after};
            Cursor_ = Count_;
        }
        MergeAllowed_ = true;
        ++Revision_;
        return true;
    }

    [[nodiscard]] bool CanUndo() const noexcept
    {
        return Cursor_ != 0 && Revision_ != ~foundation::uint64{0};
    }
    [[nodiscard]] bool CanRedo() const noexcept
    {
        return Cursor_ < Count_ && Revision_ != ~foundation::uint64{0};
    }
    // Reject a mismatched current head without changing content or history.
    [[nodiscard]] bool Undo(T& current) noexcept
    {
        if (!CanUndo() || !(current == Entries_[Cursor_ - 1]->After))
        {
            return false;
        }
        current = Entries_[--Cursor_]->Before;
        ++Revision_;
        BreakGroup();
        return true;
    }
    [[nodiscard]] bool Redo(T& current) noexcept
    {
        if (!CanRedo() || !(current == Entries_[Cursor_]->Before))
        {
            return false;
        }
        current = Entries_[Cursor_++]->After;
        ++Revision_;
        BreakGroup();
        return true;
    }
    void BreakGroup() noexcept
    {
        MergeAllowed_ = false;
    }
    // Only a new document identity may reset the monotonic revision.
    void Reset() noexcept
    {
        for (auto& entry : Entries_)
        {
            entry.reset();
        }
        Cursor_ = Count_ = 0;
        Revision_ = 0;
        BreakGroup();
    }
    [[nodiscard]] foundation::uint64 Revision() const noexcept
    {
        return Revision_;
    }

private:
    struct Entry
    {
        T Before;
        T After;
    };
    std::optional<Entry> Entries_[CAPACITY];
    foundation::usize Cursor_ = 0;
    foundation::usize Count_ = 0;
    foundation::uint64 Revision_ = 0;
    bool MergeAllowed_ = false;
};
} // namespace ludus::editor
