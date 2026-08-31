cmake_minimum_required(VERSION 3.28)
project(AsahiSpeak)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(SFML_DIR ${CMAKE_CURRENT_SOURCE_DIR}/SFML-3.1.0)
include_directories(${SFML_DIR}/include)

add_compile_definitions(WOLFSSL_DTLS_MTU WOLFSSL_DTLS WOLFSSL_DTLS_CID)

set(WOLFSSL_OPENSSLEXTRA ON CACHE BOOL "" FORCE)
set(WOLFSSL_OPENSSLALL ON CACHE BOOL "" FORCE)
set(WOLFSSL_OPENSSLCOEXIST ON CACHE BOOL "" FORCE)
set(WOLFSSL_DTLS ON CACHE BOOL "" FORCE)
set(WOLFSSL_DTLS_CID ON CACHE BOOL "" FORCE)

add_subdirectory(cubeb)
add_subdirectory(wolfssl-5.9.2-stable)
add_subdirectory(opus-1.5.2)


add_compile_options(-O3)

set(CMAKE_BUILD_TYPE Debug)

add_executable(AsahiSpeak src/main.cpp)



target_compile_features(AsahiSpeak PRIVATE cxx_std_20)

target_link_libraries(AsahiSpeak PRIVATE cubeb wolfssl opus)


target_include_directories(AsahiSpeak PUBLIC 
    ${PROJECT_SOURCE_DIR}/cubeb/include
    ${PROJECT_SOURCE_DIR}/cubeb/src
    ${PROJECT_SOURCE_DIR}/build/exports
    ${PROJECT_SOURCE_DIR}/wolfssl-5.9.2-stable/wolfssl
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/opus-1.5.2/include
)

if(!WIN32)
    target_compile_features(AsahiSpeakServer PRIVATE cxx_std_20)

    add_executable(AsahiSpeakServer src/server.cpp)

    target_link_libraries(AsahiSpeakServer PRIVATE wolfssl)

    target_include_directories(AsahiSpeakServer PUBLIC 
        ${PROJECT_SOURCE_DIR}/wolfssl-5.9.2-stable/wolfssl
        ${PROJECT_SOURCE_DIR}/src
    )
endif()