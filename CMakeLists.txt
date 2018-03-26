cmake_minimum_required(VERSION 2.8)

project(co)

if(EXISTS ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
	include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
	conan_basic_setup()
endif()

include_directories("./include")

add_subdirectory("test")
add_subdirectory("examples")
#add_subdirectory("benchmark")

enable_testing()
add_test(NAME unit_tests COMMAND tests )
