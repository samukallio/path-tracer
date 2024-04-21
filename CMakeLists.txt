cmake_minimum_required(VERSION 3.8)

project (path-tracer)

find_package (Vulkan REQUIRED)
find_program (glslc_executable NAMES glslc HINTS Vulkan::glslc)

add_subdirectory (lib/glfw-3.4)
add_subdirectory (lib/glm-1.0.1)

set (CMAKE_CXX_STANDARD 20)

add_executable (path-tracer
	src/common.h
	src/main.cpp
	src/vulkan.h
	src/vulkan.cpp
	src/scene.h
	src/scene.cpp
	src/tiny_obj_loader.h
	src/stb_image.h
	src/stb_rect_pack.h
	#
	lib/imgui-1.90.5-docking/imgui.cpp
	lib/imgui-1.90.5-docking/imgui_draw.cpp
	lib/imgui-1.90.5-docking/imgui_tables.cpp
	lib/imgui-1.90.5-docking/imgui_widgets.cpp
	lib/imgui-1.90.5-docking/imgui_demo.cpp
)

target_include_directories (path-tracer
	PRIVATE lib/imgui-1.90.5-docking
)

target_link_libraries (path-tracer
	Vulkan::Vulkan
	glfw
	glm
)

# Create a directory for generated source files under the build
# directory, and add it as an include directory for the main program.
set (GENERATED_SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/src)
file (MAKE_DIRECTORY ${GENERATED_SOURCE_DIR})
target_include_directories (path-tracer PRIVATE ${GENERATED_SOURCE_DIR})

# Compiles a single GLSL file into one or more SPIR-V num format output
# files, one per requested shader stage type.  A single GLSL file can
# contain multiple shader stages, and these are distinguished at compile
# time by setting the VERTEX, FRAGMENT, or COMPUTE pre-processor macros.
function (compile_shader target source)
	cmake_parse_arguments (PARSE_ARGV 2 arg "" "VERTEX;FRAGMENT;COMPUTE" "DEPENDS")
	foreach (stage vertex fragment compute)
	string (TOUPPER ${stage} type)
		set (output_path ${GENERATED_SOURCE_DIR}/${arg_${type}})
		if (arg_${type})
			add_custom_command (
				OUTPUT ${output_path}
				DEPENDS ${source} ${arg_DEPENDS}
				COMMAND
					${glslc_executable}
					-D${type}=1
					-MD
					-mfmt=num
					-fshader-stage=${stage}
					-O
					-o ${output_path}
					${CMAKE_CURRENT_SOURCE_DIR}/${source}
			)
			target_sources (${target} PRIVATE ${output_path})
			source_group ("Shader Files" FILES ${output_path})
		endif ()
	endforeach ()
	target_sources (${target} PRIVATE ${source})
	source_group ("Shader Files" FILES ${source})
endfunction ()

target_sources (path-tracer PRIVATE src/common.glsl.inc)
source_group ("Shader Files" FILES src/common.glsl.inc)

compile_shader (path-tracer src/resolve.glsl
		VERTEX resolve.vertex.inc
		FRAGMENT resolve.fragment.inc
		DEPENDS src/common.glsl.inc)

compile_shader (path-tracer src/render.glsl
		COMPUTE render.compute.inc
		DEPENDS src/common.glsl.inc)

compile_shader (path-tracer src/imgui.glsl
		VERTEX imgui.vertex.inc
		FRAGMENT imgui.fragment.inc)

