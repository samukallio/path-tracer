cmake_minimum_required(VERSION 3.8)

project (path-tracer)

find_package (Vulkan REQUIRED FATAL_ERROR)

add_subdirectory (lib/glfw-3.4)
add_subdirectory (lib/glm-1.0.1)

add_executable (path-tracer
	src/main.cpp
)

set_target_properties (path-tracer PROPERTIES
	CMAKE_CXX_STANDARD 20
)

target_link_libraries (path-tracer
	Vulkan::Vulkan
	glfw
	glm
)
