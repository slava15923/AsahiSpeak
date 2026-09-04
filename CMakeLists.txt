cmake_minimum_required(VERSION 3.28)
project(AsahiSpeak)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_definitions(WOLFSSL_DTLS_MTU WOLFSSL_DTLS WOLFSSL_DTLS_CID)

set(WOLFSSL_OPENSSLEXTRA ON CACHE BOOL "" FORCE)
set(WOLFSSL_OPENSSLALL ON CACHE BOOL "" FORCE)
set(WOLFSSL_OPENSSLCOEXIST ON CACHE BOOL "" FORCE)
set(WOLFSSL_DTLS ON CACHE BOOL "" FORCE)
set(WOLFSSL_DTLS_CID ON CACHE BOOL "" FORCE)

set(CLI11_BUILD_TESTS OFF CACHE BOOL "Disable CLI11 tests")

add_subdirectory(cubeb)
add_subdirectory(wolfssl-5.9.2-stable)
add_subdirectory(opus-1.5.2)
add_subdirectory(ReNameNoise)
add_subdirectory(CLI11)

add_compile_options(-O3)

set(CMAKE_BUILD_TYPE Debug)

add_executable(AsahiSpeak src/main.cpp)



target_compile_features(AsahiSpeak PRIVATE cxx_std_20)

target_link_libraries(AsahiSpeak PRIVATE cubeb wolfssl opus renamenoise CLI11::CLI11)


target_include_directories(AsahiSpeak PUBLIC 
    ${PROJECT_SOURCE_DIR}/cubeb/include
    ${PROJECT_SOURCE_DIR}/cubeb/src
    ${PROJECT_SOURCE_DIR}/build/exports
    ${PROJECT_SOURCE_DIR}/wolfssl-5.9.2-stable/wolfssl
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/opus-1.5.2/include
    ${PROJECT_SOURCE_DIR}/ReNameNoise/include
    ${PROJECT_SOURCE_DIR}/CLI11/include/CLI
)

if(NOT WIN32)
    add_executable(AsahiSpeakServer src/server.cpp)
    target_compile_features(AsahiSpeakServer PRIVATE cxx_std_20)
    target_link_libraries(AsahiSpeakServer PRIVATE wolfssl)

    target_include_directories(AsahiSpeakServer PUBLIC 
        ${PROJECT_SOURCE_DIR}/wolfssl-5.9.2-stable/wolfssl
        ${PROJECT_SOURCE_DIR}/src
    )
endif()