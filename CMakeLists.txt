cmake_minimum_required(VERSION 3.8)

project (path-tracer)

find_package (Vulkan REQUIRED FATAL_ERROR)

set (CMAKE_CXX_STANDARD 20)

add_executable (path-tracer
	src/main.cpp
)
