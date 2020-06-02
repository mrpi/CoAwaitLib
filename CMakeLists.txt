cmake_minimum_required(VERSION 2.8)

set (CMAKE_CXX_STANDARD 17)

project(co)

if(EXISTS ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
	include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
	conan_basic_setup()
endif()

if (MSVC)
   # not required but recommended
   #set ( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /GT")
   #set ( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GT")
endif()

include_directories("./include")

add_subdirectory("test")
add_subdirectory("examples")
#add_subdirectory("benchmark")

enable_testing()
add_test(NAME unit_tests COMMAND tests )
