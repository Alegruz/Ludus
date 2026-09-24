#pragma once

// Trivial-relocation trait for Ludus containers.
//
// "Relocation" is move-construct-then-destroy-the-source. A type is *trivially
// relocatable* when that whole operation is equivalent to copying its bytes with
// memcpy (i.e. it has no self-referential internal pointers and its move has no
// observable side effects). For such a type a container may grow its storage by
// memcpy'ing the elements and skipping the per-element destructor calls.
//
// The C++ standard does not yet expose this property (P1144 / P2786 are still in
// flight and Clang 18 defines no feature-test macro for it), so Ludus uses an
// opt-in trait, exactly like Folly's IsRelocatable and EASTL's memcpy fast path:
//
//   * By default IsTriviallyRelocatable<T> is std::is_trivially_copyable_v<T>,
//     which the standard already guarantees is byte-copyable. This is the safe
//     default: the fast path is impossible to reach by accident.
//   * A type author may opt a move-safe, pointer-free type in with
//     LUDUS_TRIVIALLY_RELOCATABLE(Type). This is an explicit, auditable promise
//     that the type is bitwise-relocatable.
//
// When a future toolchain gains std::is_trivially_relocatable, this header is the
// single place to widen the default behind its feature-test macro; no container
// code changes.

#include <ludus/foundation/base/defines.h>

#include <type_traits>

namespace ludus::foundation::core
{
// Primary template: a type is trivially relocatable if it is trivially copyable.
// Specialize (via LUDUS_TRIVIALLY_RELOCATABLE) to opt a non-trivially-copyable
// but bitwise-relocatable type into the memcpy growth path.
template <typename ElementType>
struct IsTriviallyRelocatableTrait : std::bool_constant<std::is_trivially_copyable_v<ElementType>>
{
};

template <typename ElementType>
inline constexpr bool IsTriviallyRelocatable = IsTriviallyRelocatableTrait<ElementType>::value;

template <typename ElementType>
concept TriviallyRelocatable = IsTriviallyRelocatable<ElementType>;
} // namespace ludus::foundation::core

namespace ludus::foundation
{
using core::IsTriviallyRelocatable;
using core::IsTriviallyRelocatableTrait;
using core::TriviallyRelocatable;
} // namespace ludus::foundation

// Opt a type into the trivial-relocation fast path. Use ONLY for types whose
// "move then destroy source" is equivalent to a byte copy: no internal pointers
// back into the object, no external registration keyed on the object's address,
// no side-effecting move constructor/destructor. Misuse is undefined behaviour.
// Must be invoked at namespace scope.
#define LUDUS_TRIVIALLY_RELOCATABLE(Type)                                                                              \
    namespace ludus::foundation::core                                                                                  \
    {                                                                                                                  \
    template <>                                                                                                        \
    struct IsTriviallyRelocatableTrait<Type> : std::true_type                                                          \
    {                                                                                                                  \
    };                                                                                                                 \
    }
