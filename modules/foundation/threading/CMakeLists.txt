add_library(ludus_foundation_threading STATIC)
add_library(Ludus::FoundationThreading ALIAS ludus_foundation_threading)
set_target_properties(ludus_foundation_threading PROPERTIES EXPORT_NAME FoundationThreading)
target_sources(ludus_foundation_threading PRIVATE src/jobs.cpp
    PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS include
    FILES include/ludus/foundation/threading/jobs.hpp)
target_include_directories(ludus_foundation_threading PRIVATE src)
target_link_libraries(ludus_foundation_threading PUBLIC Ludus::FoundationBase)
if(EMSCRIPTEN)
    target_sources(ludus_foundation_threading PRIVATE src/native_serial.cpp)
elseif(WIN32)
    target_sources(ludus_foundation_threading PRIVATE src/native_windows.cpp)
else()
    target_sources(ludus_foundation_threading PRIVATE src/native_posix.cpp)
    find_package(Threads REQUIRED)
    target_link_libraries(ludus_foundation_threading PUBLIC Threads::Threads)
endif()
ludus_apply_project_defaults(ludus_foundation_threading)
install(TARGETS ludus_foundation_threading EXPORT LudusTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(LUDUS_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED CONFIG)
    add_executable(ludus_foundation_threading_tests tests/jobs_tests.cpp)
    target_link_libraries(ludus_foundation_threading_tests PRIVATE
        Ludus::FoundationThreading Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_foundation_threading_tests)
    ludus_enable_test_exceptions(ludus_foundation_threading_tests)
    add_test(NAME ludus_foundation_threading_tests COMMAND ludus_foundation_threading_tests)
    set_tests_properties(ludus_foundation_threading_tests PROPERTIES LABELS "unit;foundation;threading" TIMEOUT 60)
endif()

option(LUDUS_BUILD_THREADING_BENCHMARK "Build the native task scheduler benchmark" OFF)
if(LUDUS_BUILD_THREADING_BENCHMARK AND NOT EMSCRIPTEN)
    add_executable(ludus_threading_benchmark tests/benchmark.cpp)
    target_link_libraries(ludus_threading_benchmark PRIVATE Ludus::FoundationThreading)
    ludus_apply_project_defaults(ludus_threading_benchmark)
endif()

if(LUDUS_BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(ludus_threading_startup_tests tests/startup_tests.cpp)
    target_link_libraries(ludus_threading_startup_tests PRIVATE
        Ludus::FoundationThreading Catch2::Catch2WithMain)
    target_link_options(ludus_threading_startup_tests PRIVATE
        -Wl,--wrap=pthread_create -Wl,--wrap=pthread_join
        -Wl,--wrap=pthread_mutex_init -Wl,--wrap=pthread_cond_init)
    ludus_apply_project_defaults(ludus_threading_startup_tests)
    ludus_enable_test_exceptions(ludus_threading_startup_tests)
    add_test(NAME ludus_threading_startup_tests COMMAND ludus_threading_startup_tests)
    set_tests_properties(ludus_threading_startup_tests PROPERTIES LABELS "unit;foundation;threading" TIMEOUT 60)
endif()

if(LUDUS_BUILD_TESTS AND NOT LUDUS_ENABLE_ASAN AND NOT LUDUS_ENABLE_UBSAN AND NOT LUDUS_ENABLE_TSAN)
    add_executable(ludus_threading_alloc_tests tests/allocation_tests.cpp)
    target_link_libraries(ludus_threading_alloc_tests PRIVATE
        Ludus::FoundationThreading Catch2::Catch2WithMain)
    ludus_apply_project_defaults(ludus_threading_alloc_tests)
    ludus_enable_test_exceptions(ludus_threading_alloc_tests)
    add_test(NAME ludus_threading_alloc_tests COMMAND ludus_threading_alloc_tests)
    set_tests_properties(ludus_threading_alloc_tests PROPERTIES LABELS "unit;foundation;threading;alloc" TIMEOUT 60)
endif()

if(EMSCRIPTEN AND LUDUS_BUILD_WEB_PROBES)
    add_executable(ludus_threading_web_smoke tests/web_smoke.cpp)
    target_link_libraries(ludus_threading_web_smoke PRIVATE Ludus::FoundationThreading)
    ludus_apply_project_defaults(ludus_threading_web_smoke)
    target_link_options(ludus_threading_web_smoke PRIVATE
        -fno-exceptions -sENVIRONMENT=web,node -sEXIT_RUNTIME=1
        -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1)
    set_target_properties(ludus_threading_web_smoke PROPERTIES OUTPUT_NAME threading-smoke SUFFIX ".js")
    enable_testing()
    add_test(NAME ludus_threading_web_smoke COMMAND ludus_threading_web_smoke)
    set_tests_properties(ludus_threading_web_smoke PROPERTIES LABELS "web;foundation;threading" TIMEOUT 60)
endif()
