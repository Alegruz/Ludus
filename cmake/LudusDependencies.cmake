# Shared configure-time diagnostics for source builds and installed SDK helpers.
include_guard(GLOBAL)

function(ludus_dependency_error feature dependency detail recovery)
    message(FATAL_ERROR
        "Ludus setup problem: ${feature} requires ${dependency}.\n"
        "${detail}\n\n"
        "How to fix:\n  ${recovery}\n\n"
        "Then rerun CMake Configure (VS Code: CMake: Configure).\n"
        "Configuration does not download or repair dependencies automatically.")
endfunction()

# Require a host executable, resolve command names through PATH, and return its
# absolute path so Ninja never interprets a command name as a source dependency.
# Version probes are bounded and never run target-platform binaries.
function(ludus_require_tool variable feature recovery)
    set(value "${${variable}}")
    unset(resolved)
    find_program(resolved NAMES "${value}" NO_CACHE NO_CMAKE_FIND_ROOT_PATH)
    if(NOT value OR NOT resolved OR IS_DIRECTORY "${value}")
        ludus_dependency_error("${feature}" "${variable}"
            "Missing or non-executable tool: '${value}' (setting ${variable})."
            "${recovery}\n  If already installed, set ${variable} to its host executable path.")
    endif()
    if(ARGN)
        execute_process(COMMAND "${resolved}" ${ARGN} RESULT_VARIABLE result
            OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
        if(NOT result STREQUAL "0")
            ludus_dependency_error("${feature}" "${variable}"
                "Cannot run '${resolved}': ${result}\n${output}${error}"
                "${recovery}\n  Check host architecture, permissions and shared libraries, or update ${variable}.")
        endif()
    endif()
    set(${variable} "${resolved}" PARENT_SCOPE)
endfunction()

function(ludus_require_file path feature recovery)
    if(NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
        ludus_dependency_error("${feature}" "an input file"
            "Missing file: '${path}'." "${recovery}")
    endif()
endfunction()

# Macro keeps FindPython3's imported targets and variables in the caller scope.
macro(ludus_require_python feature)
    find_package(Python3 3.10 QUIET COMPONENTS Interpreter)
    if(NOT Python3_FOUND)
        ludus_dependency_error("${feature}" "Python 3.10 or newer"
            "No usable host Python interpreter was found. Python3_EXECUTABLE='${Python3_EXECUTABLE}'."
            "Install Python 3.10+; set Python3_EXECUTABLE to its executable path.")
    endif()
endmacro()
