

include_directories(.)

file(GLOB_RECURSE headers "../include/co/*.hpp")

file(GLOB serverSources asio_echo_server.cpp)
set(allServerSources ${serverSources} ${headers})
add_executable(asio_echo_server ${allServerSources})
target_link_libraries(asio_echo_server ${CONAN_LIBS} pthread)

file(GLOB clientSources asio_echo_client.cpp)
set(allClientSources ${clientSources} ${headers})
add_executable(asio_echo_client ${allClientSources})
target_link_libraries(asio_echo_client ${CONAN_LIBS} pthread)

file(GLOB readmeSources readme_example.cpp)
set(allReadmeSources ${readmeSources} ${headers})
add_executable(readme_example ${allReadmeSources})
target_link_libraries(readme_example ${CONAN_LIBS} pthread)
