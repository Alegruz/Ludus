# Ludus::Text — CPU font bytes/faces, UTF-8 validation, HarfBuzz shaping,
# FreeType metrics/rasterization. See .kiro/specs/text-font-rendering/design.md.
#
# FreeType (2.14.3) and HarfBuzz (14.5.1) are PRIVATE dependencies: their types
# never appear in a public header, and their headers are consumed as system
# includes (Conan marks them SYSTEM) so the project warning/tidy policy is not
# applied to vendored code (requirement T15, design section 2).

find_package(freetype CONFIG REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)

# Integration correction (recorded in the F0 evidence): the FreeType 2.14.3
# ConanCenter package consumed in this environment does not propagate its
# `include/freetype2` directory through the imported target's interface include
# dirs (the generated `freetype_INCLUDE_DIRS_*` is empty even though the recipe
# appends it). FreeType headers live at `<pkg>/include/freetype2/ft2build.h`.
# Resolve that directory robustly via find_path, seeded with the per-config
# package folder Conan does record, and attach it as a SYSTEM include so vendor
# headers stay outside the project warning/tidy policy.
set(_ludus_ft_hints "")
foreach(_cfg RELWITHDEBINFO DEBUG RELEASE MINSIZEREL "")
    if(_cfg STREQUAL "")
        set(_var freetype_PACKAGE_FOLDER)
    else()
        set(_var freetype_PACKAGE_FOLDER_${_cfg})
    endif()
    if(DEFINED ${_var})
        list(APPEND _ludus_ft_hints "${${_var}}/include/freetype2" "${${_var}}/include")
    endif()
endforeach()
find_path(LUDUS_FREETYPE_INCLUDE_DIR
    NAMES ft2build.h
    HINTS ${_ludus_ft_hints}
    NO_DEFAULT_PATH
)
if(NOT LUDUS_FREETYPE_INCLUDE_DIR)
    message(FATAL_ERROR "Ludus::Text could not locate ft2build.h from the FreeType package")
endif()

add_library(ludus_text STATIC)
add_library(Ludus::Text ALIAS ludus_text)

set_target_properties(ludus_text PROPERTIES
    OUTPUT_NAME ludus_text
    EXPORT_NAME Text
)

target_sources(ludus_text
    PRIVATE
        src/status.cpp
        src/utf8.cpp
        src/font_system.cpp
        src/raster_layout.cpp
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS include
        FILES
            include/ludus/text/status.h
            include/ludus/text/font_system.h
)

target_include_directories(ludus_text PRIVATE src)
target_include_directories(ludus_text SYSTEM PRIVATE "${LUDUS_FREETYPE_INCLUDE_DIR}")

target_link_libraries(ludus_text
    PUBLIC
        Ludus::FoundationBase
    PRIVATE
        Ludus::FoundationContainers
        Ludus::FoundationLogging
        Freetype::Freetype
        harfbuzz::harfbuzz
)

ludus_apply_project_defaults(ludus_text)

install(TARGETS ludus_text
    EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

if(LUDUS_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED CONFIG)

    add_executable(ludus_text_tests)
    target_sources(ludus_text_tests
        PRIVATE
            tests/utf8_tests.cpp
            tests/shaping_tests.cpp
            tests/rasterization_tests.cpp
            tests/lifetime_tests.cpp
            tests/audit_tests.cpp
    )
    target_include_directories(ludus_text_tests PRIVATE src tests)
    # Fixture font locations are injected so tests load real packaged bytes
    # without a system-font or network assumption.
    target_compile_definitions(ludus_text_tests PRIVATE
        LUDUS_TEXT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )
    target_link_libraries(ludus_text_tests
        PRIVATE
            Ludus::Text
            Catch2::Catch2WithMain
    )
    ludus_apply_project_defaults(ludus_text_tests)
    ludus_enable_test_exceptions(ludus_text_tests)
    add_test(NAME ludus_text_tests COMMAND ludus_text_tests)
    set_tests_properties(ludus_text_tests PROPERTIES LABELS "unit;text" TIMEOUT 120)
endif()
