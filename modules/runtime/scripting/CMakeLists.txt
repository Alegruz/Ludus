# S1 is experimental and deliberately absent from the installed SDK. The reviewed
# longjmp profile has not completed production/platform acceptance (luau-s0.md).
include("${PROJECT_SOURCE_DIR}/cmake/LuauProfile.cmake")
ludus_prepare_luau()
add_library(ludus_runtime_scripting STATIC src/runtime.cpp src/state.cpp)
target_include_directories(ludus_runtime_scripting PRIVATE src)
target_link_libraries(ludus_runtime_scripting PRIVATE Luau.VM Ludus::FoundationBase Ludus::FoundationMemory Ludus::FoundationMath)
ludus_apply_project_defaults(ludus_runtime_scripting)
set_target_properties(ludus_runtime_scripting PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(EMSCRIPTEN)
    target_compile_options(ludus_runtime_scripting PRIVATE -sSUPPORT_LONGJMP=wasm)
    target_link_options(ludus_runtime_scripting INTERFACE
        -fno-exceptions -sSUPPORT_LONGJMP=wasm -sENVIRONMENT=web,node
        -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1)
endif()
