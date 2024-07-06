cmake_minimum_required(VERSION 3.8)

project (path-tracer)

find_package (Vulkan REQUIRED)
find_program (glslc_executable NAMES glslc HINTS Vulkan::glslc)

add_subdirectory (lib/glfw-3.4)
add_subdirectory (lib/glm-1.0.1)
add_subdirectory (lib/nativefiledialog-extended)

set (CMAKE_CXX_STANDARD 20)

add_executable (path-tracer
	src/core/common.h
	src/core/spectrum.h
	src/core/spectrum.cpp
	src/core/json.hpp
	src/core/miniz.h
	src/core/miniz.c
	src/core/stb_image.h
	src/core/stb_rect_pack.h
	src/core/stb.cpp
	src/core/tiny_obj_loader.h
	src/core/tiny_obj_loader.cpp
	src/scene/scene.h
	src/scene/scene.cpp
	src/scene/serializer.cpp
	src/scene/trace.cpp
	src/scene/material.h
	src/scene/material_openpbr.h
	src/application/application.h
	src/application/ui.cpp
	src/application/ui_font.h
	src/application/main.cpp
	src/renderer/vulkan.h
	src/renderer/vulkan.cpp
	#
	lib/imgui-1.90.5-docking/imgui.cpp
	lib/imgui-1.90.5-docking/imgui_draw.cpp
	lib/imgui-1.90.5-docking/imgui_tables.cpp
	lib/imgui-1.90.5-docking/imgui_widgets.cpp
	lib/imgui-1.90.5-docking/imgui_demo.cpp
	lib/imgui-1.90.5-docking/misc/cpp/imgui_stdlib.cpp
)

target_include_directories (path-tracer
	PRIVATE src/
	PRIVATE lib/imgui-1.90.5-docking
)

target_link_libraries (path-tracer
	Vulkan::Vulkan
	glfw
	glm
	nfd
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
					-I ${CMAKE_CURRENT_SOURCE_DIR}/src
					-D${type}=1
					-MD
					-mfmt=num
					-fshader-stage=${stage}
					-g
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

target_sources (path-tracer PRIVATE
	src/core/common.glsl.inc
	src/scene/scene.glsl.inc
	src/scene/trace.glsl.inc
	src/scene/material_openpbr.glsl.inc
)

source_group ("Shader Files" FILES
	src/core/common.glsl.inc
	src/scene/scene.glsl.inc
	src/scene/trace.glsl.inc
	src/scene/material_openpbr.glsl.inc
)

compile_shader (path-tracer src/renderer/imgui.glsl
		VERTEX imgui.vertex.inc
		FRAGMENT imgui.fragment.inc)

compile_shader (path-tracer src/renderer/resolve.glsl
		VERTEX resolve.vertex.inc
		FRAGMENT resolve.fragment.inc
		DEPENDS src/core/common.glsl.inc)

compile_shader (path-tracer src/renderer/preview.glsl
		COMPUTE preview.compute.inc
		DEPENDS src/core/common.glsl.inc)

compile_shader (path-tracer src/renderer/udpt_trace.glsl
		COMPUTE udpt_trace.compute.inc
		DEPENDS src/core/common.glsl.inc)

compile_shader (path-tracer src/renderer/udpt_path.glsl
		COMPUTE udpt_path.compute.inc
		DEPENDS src/core/common.glsl.inc)

