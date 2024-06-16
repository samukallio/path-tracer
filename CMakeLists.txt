cmake_minimum_required(VERSION 3.8)

project (path-tracer)

find_package (Vulkan REQUIRED)
find_program (glslc_executable NAMES glslc HINTS Vulkan::glslc)

add_subdirectory (lib/glfw-3.4)
add_subdirectory (lib/glm-1.0.1)
add_subdirectory (lib/nativefiledialog-extended)

set (CMAKE_CXX_STANDARD 20)

add_executable (path-tracer
	src/path-tracer.h
	src/application/application.h
	src/application/ui.cpp
	src/application/ui_font.h
	src/application/main.cpp
	src/renderer/common.h
	src/renderer/vulkan.h
	src/renderer/vulkan.cpp
	src/scene/scene.h
	src/scene/scene.cpp
	src/scene/spectrum.cpp
	src/scene/serializer.cpp
	src/scene/trace.cpp
	src/utility/json.hpp
	src/utility/miniz.h
	src/utility/miniz.c
	src/utility/stb_image.h
	src/utility/stb_rect_pack.h
	src/utility/stb.cpp
	src/utility/tiny_obj_loader.h
	src/utility/tiny_obj_loader.cpp
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

target_sources (path-tracer PRIVATE src/renderer/common.glsl.inc)
source_group ("Shader Files" FILES src/renderer/common.glsl.inc)

compile_shader (path-tracer src/renderer/resolve.glsl
		VERTEX resolve.vertex.inc
		FRAGMENT resolve.fragment.inc
		DEPENDS src/renderer/common.glsl.inc)

compile_shader (path-tracer src/renderer/render.glsl
		COMPUTE render.compute.inc
		DEPENDS src/renderer/common.glsl.inc)

compile_shader (path-tracer src/renderer/imgui.glsl
		VERTEX imgui.vertex.inc
		FRAGMENT imgui.fragment.inc)

