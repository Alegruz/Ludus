#pragma once

// Thanks to Meta/Folly contributors, "folly/Traits.h", "IsRelocatable":
// https://github.com/facebook/folly/blob/4dda9bd1227b5fa24c8c83c4fa515d0f980534ff/folly/docs/Traits.md .
// Electronic Arts/EASTL contributors, "vector.h", DoGrow/DoRealloc, and
// "memory.h", uninitialized_copy_impl (trait-selected byte copying):
// https://github.com/electronicarts/EASTL/blob/e323f67e5d93fdadcd44f7998069d804697b1945/include/EASTL/vector.h
// https://github.com/electronicarts/EASTL/blob/e323f67e5d93fdadcd44f7998069d804697b1945/include/EASTL/memory.h .
// Folly's relocation opt-in and EASTL's byte-copy dispatch inform this independent
// Ludus trait; EASTL's growth also destroys the old elements.
// Thanks also to Arthur O'Dwyer, "std::is_trivially_relocatable", WG21 P1144R12
// (2024), sections 2 and 3.3, for relocation terminology and trait fallback:
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p1144r12.html .
// Alisdair Meredith, Mungo Gill, Joshua Berne, Corentin Jabot, Pablo Halpern and
// Lori Hughes, "Trivial Relocatability For C++26", WG21 P2786R13 (2025), section 7:
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2786r13.html . Thank
// you for the comparison of relocation/replacement; neither proposal's language
// machinery is implemented here. Keep a C++23 trivially-copyable default plus
// explicit author opt-in. Detailed review: docs/architecture/containers.md.
//
// Trivial-relocation trait for Ludus containers.
//
// "Relocation" is move-construct-then-destroy-the-source. A type is *trivially
// relocatable* when that whole operation is equivalent to copying its bytes with
// memcpy (i.e. it has no self-referential internal pointers and its move has no
// observable side effects). For such a type a container may grow its storage by
// memcpy'ing the elements and skipping the per-element destructor calls.
//
// C++23 provides no portable std::is_trivially_relocatable trait. Ludus does not
// use Clang's __is_trivially_relocatable intrinsic; it uses an explicit opt-in
// trait, drawing on Folly's IsRelocatable and EASTL's byte-copy dispatch:
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

#include <ludus/foundation/base/core.h>

#include <type_traits>

namespace ludus::foundation::core
{
/// @brief Primary template: a type is trivially relocatable if it is trivially copyable.
///
/// Specialize (via LUDUS_TRIVIALLY_RELOCATABLE) to opt a non-trivially-copyable
/// but bitwise-relocatable type into the memcpy growth path.
template <typename ElementType>
struct IsTriviallyRelocatableTrait : std::bool_constant<std::is_trivially_copyable_v<ElementType>>
{
};

/// @brief True when containers may relocate ElementType by copying bytes and skipping destruction of the source.
///
/// Defaults to trivially copyable; extend only with LUDUS_TRIVIALLY_RELOCATABLE.
template <typename ElementType>
// Clang-tidy 18 with the web SDK misclassifies dependent constexpr
// initializers; constexpr forbids dynamic initialization here.
// NOLINTNEXTLINE(bugprone-dynamic-static-initializers)
inline constexpr bool IsTriviallyRelocatable = IsTriviallyRelocatableTrait<ElementType>::value;

/// Concept form of IsTriviallyRelocatable.
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
