#include <ludus/foundation/threading/jobs.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cerrno>
#include <pthread.h>

using namespace ludus::foundation;
using namespace ludus::foundation::threading;

namespace
{
std::atomic<uint32> gStarts{0};
std::atomic<uint32> gJoins{0};
std::atomic<uint32> gFailStart{0};
bool gFailMutex = false;
bool gFailCondition = false;
} // namespace

// GNU linker --wrap requires these exact external symbol names.
// NOLINTBEGIN(bugprone-reserved-identifier)
extern "C" int __real_pthread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
extern "C" int __real_pthread_join(pthread_t, void**);
extern "C" int __real_pthread_mutex_init(pthread_mutex_t*, const pthread_mutexattr_t*);
extern "C" int __real_pthread_cond_init(pthread_cond_t*, const pthread_condattr_t*);

extern "C" int
__wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attributes, void* (*entry)(void*), void* context)
{
    const uint32 call = ++gStarts;
    if (call == gFailStart.load())
    {
        return EAGAIN;
    }
    return __real_pthread_create(thread, attributes, entry, context);
}
extern "C" int __wrap_pthread_join(pthread_t thread, void** result)
{
    ++gJoins;
    return __real_pthread_join(thread, result);
}
extern "C" int __wrap_pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attributes)
{
    return gFailMutex ? EAGAIN : __real_pthread_mutex_init(mutex, attributes);
}
extern "C" int __wrap_pthread_cond_init(pthread_cond_t* condition, const pthread_condattr_t* attributes)
{
    return gFailCondition ? EAGAIN : __real_pthread_cond_init(condition, attributes);
}

// NOLINTEND(bugprone-reserved-identifier)

TEST_CASE("Partial native startup joins every already-started worker and can retry", "[threading]")
{
    JobSystem system;
    for (const uint32 failAt : {1u, 2u, 4u})
    {
        gStarts = 0;
        gJoins = 0;
        gFailStart = failAt;
        const auto status = system.Initialize({4});
        gFailStart = 0;
        REQUIRE(status == JobStatus::ThreadCreationFailed);
        REQUIRE(gJoins == failAt - 1);
        REQUIRE(system.Initialize({2}) == JobStatus::Ok);
        REQUIRE(system.Shutdown() == JobStatus::Ok);
        REQUIRE(gJoins == failAt + 1);
    }
}

TEST_CASE("Native synchronization initialization failure leaves a reusable system", "[threading]")
{
    JobSystem system;
    gFailMutex = true;
    const auto mutexStatus = system.Initialize({1});
    gFailMutex = false;
    REQUIRE(mutexStatus == JobStatus::ThreadCreationFailed);
    gFailCondition = true;
    const auto conditionStatus = system.Initialize({1});
    gFailCondition = false;
    REQUIRE(conditionStatus == JobStatus::ThreadCreationFailed);
    REQUIRE(system.Initialize({1}) == JobStatus::Ok);
}
