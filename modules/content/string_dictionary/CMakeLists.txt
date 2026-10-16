# Ludus::StringDictionary validates and reads LUSTR cooked string dictionaries at
# runtime. Ludus::StringDictionaryWriter deduplicates, sorts and encodes them for
# cookers; runtime consumers never need to link the writer.
add_library(ludus_string_dictionary STATIC src/string_dictionary.cpp)
add_library(Ludus::StringDictionary ALIAS ludus_string_dictionary)
set_target_properties(ludus_string_dictionary PROPERTIES EXPORT_NAME StringDictionary)
target_sources(ludus_string_dictionary PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include
    FILES include/ludus/content/string_dictionary/string_dictionary.h)
target_link_libraries(ludus_string_dictionary
    PUBLIC Ludus::FoundationBase Ludus::FoundationContainers Ludus::FoundationStrings Ludus::StringDictionaryFormat)
ludus_apply_project_defaults(ludus_string_dictionary)

add_library(ludus_string_dictionary_writer STATIC src/string_dictionary_writer.cpp)
add_library(Ludus::StringDictionaryWriter ALIAS ludus_string_dictionary_writer)
set_target_properties(ludus_string_dictionary_writer PROPERTIES EXPORT_NAME StringDictionaryWriter)
target_sources(ludus_string_dictionary_writer PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include
    FILES include/ludus/content/string_dictionary/string_dictionary_writer.h)
target_link_libraries(ludus_string_dictionary_writer PUBLIC Ludus::StringDictionary Ludus::FoundationMemory)
ludus_apply_project_defaults(ludus_string_dictionary_writer)

install(TARGETS ludus_string_dictionary ludus_string_dictionary_writer EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(LUDUS_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED CONFIG)
    add_executable(ludus_string_dictionary_tests tests/string_dictionary_tests.cpp)
    target_link_libraries(ludus_string_dictionary_tests
        PRIVATE Ludus::StringDictionary Ludus::StringDictionaryWriter Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_string_dictionary_tests)
    ludus_enable_test_exceptions(ludus_string_dictionary_tests)
    add_test(NAME ludus_string_dictionary_tests COMMAND ludus_string_dictionary_tests)
    set_tests_properties(ludus_string_dictionary_tests PROPERTIES LABELS "unit;content;strings" TIMEOUT 120)
endif()
