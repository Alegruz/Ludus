# Private miniaudio dependency, pinned and locked by source hash (design
# section 2; audio decision log A0). The exact bytes are vendored in this
# directory. We verify the hash at configure time (no network), compile the
# single-header implementation once as C, and expose it as a PRIVATE static
# dependency of Ludus::Audio. C++ warning/exception options are NOT applied to
# this C target; third-party warnings are not treated as errors.

set(LUDUS_MINIAUDIO_EXPECTED_SHA256
    "7e4f3f13c8fe66df2080ac3dd12a89193e3c2463cb7f067c798abd7331cd8ee6")
set(LUDUS_MINIAUDIO_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/miniaudio.h")

if(NOT EXISTS "${LUDUS_MINIAUDIO_SOURCE}")
    message(FATAL_ERROR
        "miniaudio source cache is missing: ${LUDUS_MINIAUDIO_SOURCE}\n"
        "Restore the locked source (0.11.23, sha256 ${LUDUS_MINIAUDIO_EXPECTED_SHA256}). "
        "No download occurs during build/configure; see third_party/miniaudio/SOURCE-LOCK.json.")
endif()

file(SHA256 "${LUDUS_MINIAUDIO_SOURCE}" LUDUS_MINIAUDIO_ACTUAL_SHA256)
if(NOT LUDUS_MINIAUDIO_ACTUAL_SHA256 STREQUAL LUDUS_MINIAUDIO_EXPECTED_SHA256)
    message(FATAL_ERROR
        "miniaudio source hash mismatch.\n"
        "  expected ${LUDUS_MINIAUDIO_EXPECTED_SHA256}\n"
        "  actual   ${LUDUS_MINIAUDIO_ACTUAL_SHA256}\n"
        "The vendored miniaudio.h does not match the pinned 0.11.23 lock. "
        "Restore the exact locked bytes (see SOURCE-LOCK.json).")
endif()
message(STATUS "miniaudio 0.11.23 source verified (sha256 ${LUDUS_MINIAUDIO_ACTUAL_SHA256})")

# Enable the C language only for this directory's targets. The root project
# declares CXX only; integrating a pinned C dependency is a deliberate, scoped
# step (design section 2).
enable_language(C)

if(EMSCRIPTEN)
    set(LUDUS_MINIAUDIO_IMPL "${CMAKE_CURRENT_SOURCE_DIR}/ludus_miniaudio_web.c")
else()
    set(LUDUS_MINIAUDIO_IMPL "${CMAKE_CURRENT_SOURCE_DIR}/ludus_miniaudio_native.c")
endif()

add_library(ludus_miniaudio STATIC "${LUDUS_MINIAUDIO_IMPL}")
add_library(Ludus::Miniaudio ALIAS ludus_miniaudio)

target_include_directories(ludus_miniaudio PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")

set_target_properties(ludus_miniaudio PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON          # miniaudio uses common POSIX/compiler extensions
    POSITION_INDEPENDENT_CODE ON)

# Third-party C: do not inherit the project's C++ warning/exception interface.
# Keep its own warnings quiet so a dependency does not fail warnings-as-errors.
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ludus_miniaudio PRIVATE -w)
endif()

# Native miniaudio device/threading needs pthread/dl/m on Linux.
if(NOT EMSCRIPTEN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Threads REQUIRED)
    target_link_libraries(ludus_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    target_link_libraries(ludus_miniaudio PUBLIC m)
endif()

# Installed SDK static closure: a PRIVATE link from Ludus::Audio still needs this
# archive present for downstream static linking (design section 2).
install(TARGETS ludus_miniaudio EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE-MIT-0.txt"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${LUDUS_ENGINE_PACKAGE_NAME}/licenses"
    RENAME miniaudio-LICENSE.txt)

set(_audio_dependency_sanitizers "")
if(LUDUS_ENABLE_ASAN)
    list(APPEND _audio_dependency_sanitizers address)
endif()
if(LUDUS_ENABLE_UBSAN)
    list(APPEND _audio_dependency_sanitizers undefined)
endif()
if(LUDUS_ENABLE_TSAN)
    list(APPEND _audio_dependency_sanitizers thread)
endif()
if(_audio_dependency_sanitizers)
    list(JOIN _audio_dependency_sanitizers "," _audio_dependency_sanitizer_flags)
    target_compile_options(ludus_miniaudio PRIVATE -fsanitize=${_audio_dependency_sanitizer_flags} -fno-omit-frame-pointer)
endif()
