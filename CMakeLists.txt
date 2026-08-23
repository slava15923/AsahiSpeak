cmake_minimum_required(VERSION 3.28)
project(AsahiSpeak)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(SFML_DIR ${CMAKE_CURRENT_SOURCE_DIR}/SFML-3.1.0)
include_directories(${SFML_DIR}/include)


set(MINIMAL_SOCKET_BUILD_SAMPLES OFF CACHE BOOL "Disable building samples")
#add_subdirectory(SFML-3.1.0)
add_subdirectory(cubeb)
add_subdirectory(libhv)

set(CMAKE_BUILD_TYPE Debug)

add_executable(AsahiSpeak src/main.cpp)
add_executable(AsahiSpeakServer src/server.cpp)
add_subdirectory(wolfssl-5.9.2-stable)

target_compile_features(AsahiSpeak PRIVATE cxx_std_17)

target_link_libraries(AsahiSpeak PRIVATE cubeb hv wolfssl)
target_link_libraries(AsahiSpeakServer PRIVATE hv wolfssl)

target_include_directories(AsahiSpeak PUBLIC 
    ${PROJECT_SOURCE_DIR}/ringbuf/include
    ${PROJECT_SOURCE_DIR}/cubeb/include
    ${PROJECT_SOURCE_DIR}/cubeb/src
    ${PROJECT_SOURCE_DIR}/build/exports
    ${PROJECT_SOURCE_DIR}/libhv/include/hv
    ${PROJECT_SOURCE_DIR}/wolfssl-5.9.2-stable/wolfssl
    ${PROJECT_SOURCE_DIR}/src
)

target_include_directories(AsahiSpeakServer PUBLIC 
    ${PROJECT_SOURCE_DIR}/ringbuf/include
    ${PROJECT_SOURCE_DIR}/cubeb/include
    ${PROJECT_SOURCE_DIR}/cubeb/src
    ${PROJECT_SOURCE_DIR}/build/exports
    ${PROJECT_SOURCE_DIR}/libhv/include/hv
    ${PROJECT_SOURCE_DIR}/wolfssl-5.9.2-stable/wolfssl
    ${PROJECT_SOURCE_DIR}/src
)