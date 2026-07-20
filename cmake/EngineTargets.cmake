function(ludus_apply_project_defaults target_name)
    target_link_libraries(
        ${target_name}
        PRIVATE
            $<BUILD_INTERFACE:ludus_project_options>
            $<BUILD_INTERFACE:ludus_project_warnings>
    )
endfunction()
