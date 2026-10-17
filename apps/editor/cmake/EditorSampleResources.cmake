# Collects the files of the editor's bundled samples for Qt resources.
#
# Generated build outputs (`out/`, `build/`), editor state (`.ludus/`) and local
# machine settings (`CMakeUserPresets.json`) inside a sample are never part of
# the sample library. They are matched on paths relative to each sample root, so
# a checkout that itself lives below an `out/` or `build/` directory (for
# example `<repo>/out/worktrees/<task>` or `~/build/Ludus`) keeps every sample
# file. tests/build_contract/verify_editor_sample_resources.py guards this.
#
# ludus_collect_editor_sample_files(<out-variable> <examples-root> <sample>...)
#   Sets <out-variable> to the absolute paths of the files below
#   <examples-root>/<sample> for each listed sample, excluding generated content.
function(ludus_collect_editor_sample_files out_variable examples_root)
    # Script mode (the build-contract check) cannot register glob dependencies.
    set(configure_depends CONFIGURE_DEPENDS)
    if(CMAKE_SCRIPT_MODE_FILE)
        set(configure_depends)
    endif()
    set(collected_files)
    foreach(sample IN LISTS ARGN)
        set(sample_root "${examples_root}/${sample}")
        file(GLOB_RECURSE sample_files ${configure_depends} RELATIVE "${sample_root}"
            "${sample_root}/*")
        list(FILTER sample_files EXCLUDE REGEX
            "(^|/)(out|build|\\.ludus)/|(^|/)CMakeUserPresets\\.json$")
        list(TRANSFORM sample_files PREPEND "${sample_root}/")
        list(APPEND collected_files ${sample_files})
    endforeach()
    set(${out_variable} "${collected_files}" PARENT_SCOPE)
endfunction()
