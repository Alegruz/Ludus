// Negative compilation cases for UniquePtr (docs/architecture/smart-pointers.md,
// slice 1). tests/build_contract/verify_pointer_diagnostics.py compiles this file
// once per LUDUS_POINTER_CASE value and requires each case to fail with its
// expected diagnostic. Without a case the file must compile: that control run
// proves the replayed compiler flags are valid, so a failure means the rule.
#include <ludus/foundation/base/core.h>
#include <ludus/foundation/base/pointer.hpp>

namespace ludus_pointer_compile_failures
{
using ludus::foundation::int32;
using ludus::foundation::Move;
using ludus::foundation::UniquePtr;

#if !defined(LUDUS_POINTER_CASE)
void DestroyComplete(UniquePtr<int32>& inoutOwner) noexcept
{
    inoutOwner.Reset();
}
#elif LUDUS_POINTER_CASE == 1 // The default deleter requires a complete type.
struct Incomplete;
void DestroyIncomplete(UniquePtr<Incomplete>& inoutOwner) noexcept
{
    inoutOwner.Reset();
}
#elif LUDUS_POINTER_CASE == 2 // A supplied deleter must be nothrow callable.
struct ThrowingDeleter
{
    void operator()(int32* object) const
    {
        delete object;
    }
};
void Declare(UniquePtr<int32, ThrowingDeleter>& inoutOwner) noexcept;
#elif LUDUS_POINTER_CASE == 3 // Deletion through a non-virtual base is unsafe.
struct PlainBase
{
    int32 Value = 0;
};
struct PlainDerived final : PlainBase
{
};
UniquePtr<PlainBase> Convert(UniquePtr<PlainDerived>&& other) noexcept
{
    return UniquePtr<PlainBase>(Move(other));
}
#elif LUDUS_POINTER_CASE == 4 // A stateful deleter cannot release a bare pointer.
struct StatefulDeleter
{
    void operator()(int32* object) const noexcept
    {
        delete object;
    }
    int32 DomainId = 0;
};
int32* Escape(UniquePtr<int32, StatefulDeleter>& inoutOwner) noexcept
{
    return inoutOwner.Release();
}
#elif LUDUS_POINTER_CASE == 5 // Arrays use Ludus containers, not owners.
void Declare(UniquePtr<int32[]>& inoutOwner) noexcept;
#endif
} // namespace ludus_pointer_compile_failures
