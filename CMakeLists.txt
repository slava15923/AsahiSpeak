cmake_minimum_required(VERSION 3.28)
project(AsahiSpeak)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(SFML_DIR ${CMAKE_CURRENT_SOURCE_DIR}/SFML-3.1.0)
include_directories(${SFML_DIR}/include)

#add_subdirectory(SFML-3.1.0)
add_subdirectory(cubeb)

set(CMAKE_BUILD_TYPE Debug)

add_executable(AsahiSpeak src/main.cpp)

target_compile_features(AsahiSpeak PRIVATE cxx_std_17)

target_link_libraries(AsahiSpeak PRIVATE cubeb)

target_include_directories(AsahiSpeak PUBLIC ${PROJECT_SOURCE_DIR}/ringbuf/include)
target_include_directories(AsahiSpeak PUBLIC ${PROJECT_SOURCE_DIR}/cubeb/include)
target_include_directories(AsahiSpeak PUBLIC ${PROJECT_SOURCE_DIR}/cubeb/src)
target_include_directories(AsahiSpeak PRIVATE ${PROJECT_SOURCE_DIR}/build/exports)