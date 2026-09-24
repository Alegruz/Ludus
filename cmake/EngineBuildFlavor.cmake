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

foreach(policy LUDUS_ENABLE_ASSERTS LUDUS_BREAK_ON_CHECK LUDUS_ASSERT_POLICY_VERSION LUDUS_ASSERT_DIALOGS_AVAILABLE)
    if(DEFINED ${policy})
        message(FATAL_ERROR "${policy} is generated from LUDUS_BUILD_FLAVOR; overrides are forbidden")
    endif()
endforeach()

# Bumped to 2: enabled ASSERT/ASSERT_F is now resumable through explicit
# developer action (debugger continue, or the external-helper Continue-once
# dialog) in eligible builds. Old headers/runtime assume ASSERT is terminal, so
# the SDK policy version changes to force a recompile and reject a mismatch.
set(LUDUS_ASSERT_POLICY_VERSION 2)
set(LUDUS_ENABLE_ASSERTS 0)
set(LUDUS_BREAK_ON_CHECK 0)
if(LUDUS_BUILD_FLAVOR_ID LESS 3)
    set(LUDUS_ENABLE_ASSERTS 1)
    set(LUDUS_BREAK_ON_CHECK 1)
endif()

# Interactive-dialog capability. This is build-time ELIGIBILITY only, generated
# from the explicit flavor and the CI build setting; a runtime CI veto and the
# control-endpoint handshake gate whether a prompt actually appears. It can be 1
# only for a non-CI Debug build. CI is a build input here (defaulting from the CI
# environment) so a locally built dialog-capable binary is still marked eligible;
# the runtime veto stops it from prompting when later executed under CI.
set(LUDUS_CI_BUILD "$ENV{CI}" CACHE STRING "Set for a CI build; forces dialogs unavailable")
set(LUDUS_ASSERT_DIALOGS_AVAILABLE 0)
if(LUDUS_BUILD_FLAVOR_ID EQUAL 1 AND NOT LUDUS_CI_BUILD)
    set(LUDUS_ASSERT_DIALOGS_AVAILABLE 1)
endif()

string(TOUPPER "${LUDUS_BUILD_FLAVOR}" LUDUS_BUILD_FLAVOR_DEFINE)
set(LUDUS_SDK_VARIANT
    "assert-v${LUDUS_ASSERT_POLICY_VERSION}-${LUDUS_BUILD_FLAVOR}-${CMAKE_BUILD_TYPE}-dialogs-${LUDUS_ASSERT_DIALOGS_AVAILABLE}-asan-${LUDUS_ENABLE_ASAN}-ubsan-${LUDUS_ENABLE_UBSAN}-tsan-${LUDUS_ENABLE_TSAN}")
