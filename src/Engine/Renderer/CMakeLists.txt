set(LUDUS_RENDERER_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/Renderer.cpp"
)

add_library(LudusRenderer SHARED ${LUDUS_RENDERER_SOURCES})

foreach(LUDUS_RENDERER_SOURCE IN LISTS LUDUS_RENDERER_SOURCES)
	if(NOT EXISTS "${LUDUS_RENDERER_SOURCE}")
		ludus_fatal_error("Expected renderer source missing: ${LUDUS_RENDERER_SOURCE}")
	endif()
endforeach()

if(WIN32)
	set_target_properties(LudusRenderer PROPERTIES
		WINDOWS_EXPORT_ALL_SYMBOLS ON
	)
endif()

target_include_directories(LudusRenderer
	PUBLIC
		${CMAKE_SOURCE_DIR}/include
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(LudusRenderer
	PUBLIC
		LudusRHI
		LudusCore
		LudusBuildSettings
)

# Generate debug symbols for MSVC
if(MSVC)
    target_compile_options(LudusRenderer PRIVATE $<$<CONFIG:Debug>:/Zi>)
    target_link_options(LudusRenderer PRIVATE $<$<CONFIG:Debug>:/DEBUG>)
endif()

if(TARGET mimalloc)
	target_link_libraries(LudusRenderer PRIVATE mimalloc)
	add_dependencies(LudusRenderer mimalloc)
endif()

set_target_properties(LudusRenderer PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
	LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
	ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
	RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin
	LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib
	ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib
	RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
	LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib
	ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib
)
