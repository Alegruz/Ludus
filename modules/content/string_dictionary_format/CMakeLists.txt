# Ludus::StringDictionaryFormat: header-only LUSTR cooked string dictionary
# contract shared by the dictionary writer and the validating runtime reader
# (owned by neither tools nor runtime).
add_library(ludus_string_dictionary_format INTERFACE)
add_library(Ludus::StringDictionaryFormat ALIAS ludus_string_dictionary_format)
set_target_properties(ludus_string_dictionary_format PROPERTIES EXPORT_NAME StringDictionaryFormat)
target_sources(ludus_string_dictionary_format INTERFACE FILE_SET public_headers TYPE HEADERS BASE_DIRS include
    FILES include/ludus/content/string_dictionary_format/lustr.h)
target_link_libraries(ludus_string_dictionary_format INTERFACE Ludus::FoundationBase)
install(TARGETS ludus_string_dictionary_format EXPORT LudusTargets
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(LUDUS_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED CONFIG)
    add_executable(ludus_string_dictionary_format_tests tests/string_dictionary_format_tests.cpp)
    target_link_libraries(ludus_string_dictionary_format_tests PRIVATE Ludus::StringDictionaryFormat Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_string_dictionary_format_tests)
    ludus_enable_test_exceptions(ludus_string_dictionary_format_tests)
    add_test(NAME ludus_string_dictionary_format_tests COMMAND ludus_string_dictionary_format_tests)
    set_tests_properties(ludus_string_dictionary_format_tests PROPERTIES LABELS "unit;content")
endif()
