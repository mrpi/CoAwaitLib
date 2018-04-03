
include_directories(.)

file(GLOB sources "*.hpp" "*.cpp")
file(GLOB_RECURSE headers "../include/co/*.hpp")

set(allSources ${sources} ${headers})

add_executable(tests ${allSources})
target_link_libraries(tests ${CONAN_LIBS} pthread)
