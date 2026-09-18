cmake_minimum_required(VERSION 3.28)
project(AsahiSpeak)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ASAHI_NO_COMPILE_CLIENT "Enable custom cryptography mode" OFF)

if (MINGW)
    # Принудительно собираем все библиотеки как статические
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries only" FORCE)
    
    # Глобальные флаги для линковщика (применяются ко всем исполняемым файлам)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
    
    # Дополнительно прописываем эти же флаги в CXX_FLAGS (иногда требуется)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
endif()

if (NOT WIN32)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=mold")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=mold")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -march=native -ffast-math -pipe")
endif()



add_compile_definitions(WOLFSSL_DTLS_MTU WOLFSSL_DTLS WOLFSSL_DTLS_CID)

set(WOLFSSL_EXAMPLES OFF CACHE BOOL "Disable wolfSSL examples" FORCE)
set(WOLFSSL_CRYPT_TESTS OFF CACHE BOOL "Disable wolfSSL crypt tests" FORCE)
set(WOLFSSL_CRYPT_TESTS_LIBS OFF CACHE BOOL "Disable wolfCrypt test executable" FORCE)
set(WOLFSSL_BUILD_TESTS OFF CACHE BOOL "Disable wolfSSL build tests" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disable general testing" FORCE)

set(WOLFSSL_DTLS ON CACHE BOOL "Enable DTLS" FORCE)
set(WOLFSSL_TLS13 ON CACHE BOOL "Enable TLS 1.3" FORCE)
set(WOLFSSL_DTLS13 ON CACHE BOOL "Enable DTLS 1.3" FORCE)
set(WOLFSSL_DTLS_CID ON CACHE BOOL "Enable DTLS CID" FORCE)

set(WOLFSSL_USER_IO OFF CACHE BOOL "Enable wolfSSL User IO callbacks" FORCE)

set(WOLFSSL_OPENSSLEXTRA ON CACHE BOOL "Enable OpenSSL Extra" FORCE)
set(WOLFSSL_OPENSSLALL ON CACHE BOOL "Enable OpenSSL All" FORCE)
set(WOLFSSL_OPENSSLCOEXIST ON CACHE BOOL "Enable OpenSSL Coexist" FORCE)
set(DEBUG_WOLFSSL OFF CACHE BOOL "Disable wolfSSL debug" FORCE)
set(WOLFSSL_DEBUG OFF CACHE BOOL "Disable wolfSSL debug" FORCE)
set(WOLFSSL_CRYPT_TESTS_LIBS OFF CACHE BOOL "Disable wolfCrypt test executable" FORCE)

set(CLI11_BUILD_TESTS OFF CACHE BOOL "Disable CLI11 tests")
set(BUILD_TESTS OFF CACHE BOOL "Disable cubeb tests" FORCE)

set(WOLFSSL_CURVE25519 "yes" CACHE STRING "Enable X25519" FORCE)
set(WOLFSSL_ECC "yes" CACHE STRING "Enable ECC P-256" FORCE)

add_compile_definitions(
    WOLFSSL_DTLS_MTU 
    WOLFSSL_DTLS 
    WOLFSSL_DTLS_CID 
    WOLFSSL_TLS13 
    WOLFSSL_DTLS13
)

set(CLI11_BUILD_TESTS OFF CACHE BOOL "Disable CLI11 tests")

set(BUILD_TESTS OFF CACHE BOOL "Disable cubeb tests" FORCE)


add_subdirectory(wolfssl-5.9.2-stable)

#set(CMAKE_BUILD_TYPE Debug)
if(NOT ASAHI_NO_COMPILE_CLIENT)
    add_subdirectory(opus-1.5.2)
    add_subdirectory(ReNameNoise)
    add_subdirectory(CLI11)
    add_subdirectory(cubeb)
    add_executable(AsahiSpeak src/main.cpp)

    #target_compile_options(AsahiSpeak PRIVATE
    #    $<$<C_COMPILER_ID:GNU,Clang>:-fno-inline -fno-inline-functions>
    #    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-inline -fno-inline-functions>
    #)

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
        ${PROJECT_SOURCE_DIR}/readerwriterqueue
    )
endif()

if(NOT WIN32)
    add_executable(AsahiSpeakServer src/server.cpp)
    target_compile_features(AsahiSpeakServer PRIVATE cxx_std_20)
    target_link_libraries(AsahiSpeakServer PRIVATE wolfssl)

    target_include_directories(AsahiSpeakServer PUBLIC 
        ${PROJECT_SOURCE_DIR}/wolfssl-5.9.2-stable/wolfssl
        ${PROJECT_SOURCE_DIR}/src
    )

    set_target_properties(AsahiSpeakServer PROPERTIES UNITY_BUILD ON)
endif()