function(ludus_configure_project_warnings target_name)
    add_library(${target_name} INTERFACE)

    set(clang_like_warnings
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wformat=2
        -Wundef
        -Wnull-dereference
        -Wdouble-promotion
        -Wimplicit-fallthrough
        -Woverloaded-virtual
        -Wnon-virtual-dtor
    )

    target_compile_options(
        ${target_name}
        INTERFACE
            $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:${clang_like_warnings}>
            $<$<AND:$<BOOL:${LUDUS_WARNINGS_AS_ERRORS}>,$<CXX_COMPILER_ID:Clang,AppleClang,GNU>>:-Werror>
    )
endfunction()
