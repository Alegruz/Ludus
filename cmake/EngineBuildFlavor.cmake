# Build flavor is engine policy; CMake configuration controls optimization and
# dependency ABI. In particular, Profile uses RelWithDebInfo, not CONFIG:Profile.
if(CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR "Ludus SDK variants currently require a single-config generator (Ninja)")
endif()

set(LUDUS_BUILD_FLAVOR "" CACHE STRING "Ludus flavor: Debug, Development, Profile, Release")
set_property(CACHE LUDUS_BUILD_FLAVOR PROPERTY STRINGS Debug Development Profile Release)

if(LUDUS_BUILD_FLAVOR STREQUAL "Debug" AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(LUDUS_BUILD_FLAVOR_ID 1)
elseif(LUDUS_BUILD_FLAVOR STREQUAL "Development" AND CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(LUDUS_BUILD_FLAVOR_ID 2)
elseif(LUDUS_BUILD_FLAVOR STREQUAL "Profile" AND CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(LUDUS_BUILD_FLAVOR_ID 3)
elseif(LUDUS_BUILD_FLAVOR STREQUAL "Release" AND CMAKE_BUILD_TYPE MATCHES "^(Release|MinSizeRel)$")
    set(LUDUS_BUILD_FLAVOR_ID 4)
else()
    message(FATAL_ERROR
        "Invalid Ludus flavor/configuration: '${LUDUS_BUILD_FLAVOR}' / '${CMAKE_BUILD_TYPE}'. "
        "Set LUDUS_BUILD_FLAVOR explicitly: Debug/Debug, Development or Profile/RelWithDebInfo, "
        "Release/Release or MinSizeRel.")
endif()

foreach(policy LUDUS_ENABLE_ASSERTS LUDUS_BREAK_ON_CHECK LUDUS_ASSERT_POLICY_VERSION)
    if(DEFINED ${policy})
        message(FATAL_ERROR "${policy} is generated from LUDUS_BUILD_FLAVOR; overrides are forbidden")
    endif()
endforeach()
set(LUDUS_ASSERT_POLICY_VERSION 1)
set(LUDUS_ENABLE_ASSERTS 0)
set(LUDUS_BREAK_ON_CHECK 0)
if(LUDUS_BUILD_FLAVOR_ID LESS 3)
    set(LUDUS_ENABLE_ASSERTS 1)
    set(LUDUS_BREAK_ON_CHECK 1)
endif()

string(TOUPPER "${LUDUS_BUILD_FLAVOR}" LUDUS_BUILD_FLAVOR_DEFINE)
set(LUDUS_SDK_VARIANT
    "assert-v1-${LUDUS_BUILD_FLAVOR}-${CMAKE_BUILD_TYPE}-asan-${LUDUS_ENABLE_ASAN}-ubsan-${LUDUS_ENABLE_UBSAN}-tsan-${LUDUS_ENABLE_TSAN}")
