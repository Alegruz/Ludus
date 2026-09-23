// Linux/glibc test-only interposition. Unlike --wrap, LD_PRELOAD also observes
// calls made inside the shared libstdc++ numeric converter. Never engine code.
#include <ludus/foundation/base/types.h>
#include <unistd.h>

using ludus::foundation::usize;
// NOLINTBEGIN(bugprone-reserved-identifier, readability-identifier-naming)
extern "C" {
extern bool ludus_assert_test_watching __attribute__((weak));
void* __libc_malloc(usize) noexcept;
void* __libc_calloc(usize, usize) noexcept;
void* __libc_realloc(void*, usize) noexcept;
void* malloc(usize size) noexcept
{
    if (&ludus_assert_test_watching != nullptr && ludus_assert_test_watching)
    {
        _exit(96);
    }
    return __libc_malloc(size);
}
void* calloc(usize count, usize size) noexcept
{
    if (&ludus_assert_test_watching != nullptr && ludus_assert_test_watching)
    {
        _exit(96);
    }
    return __libc_calloc(count, size);
}
void* realloc(void* data, usize size) noexcept
{
    if (&ludus_assert_test_watching != nullptr && ludus_assert_test_watching)
    {
        _exit(96);
    }
    return __libc_realloc(data, size);
}
}
// NOLINTEND(bugprone-reserved-identifier, readability-identifier-naming)
