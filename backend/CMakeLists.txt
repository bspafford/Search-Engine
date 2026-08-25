cmake_minimum_required(VERSION 3.16)
project(SearchEngine)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
# include(ExternalProject)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(SKIP_BUILD_TEST_SUITE ON CACHE BOOL "" FORCE)
set(PQXX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PQXX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

if(CMAKE_BUILD_TYPE MATCHES "Debug")
  add_compile_options(-fsanitize=address -g -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

FetchContent_Declare(
    libpqxx
    GIT_REPOSITORY https://github.com/jtv/libpqxx
    GIT_TAG 8.0.1 # lock to specific version
)
FetchContent_MakeAvailable(libpqxx)

set(LEXBOR_BUILD_TESTS OFF CACHE BOOL "Disable Lexbor tests" FORCE)
set(LEXBOR_BUILD_EXAMPLES OFF CACHE BOOL "Disable Lexbor examples" FORCE)
set(LEXBOR_BUILD_TOOLS OFF CACHE BOOL "Disable Lexbor tools" FORCE)

FetchContent_Declare(
    lexbor
    GIT_REPOSITORY https://github.com/lexbor/lexbor
    GIT_TAG v3.0.0
)
FetchContent_MakeAvailable(lexbor)

set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_16_0
)
FetchContent_MakeAvailable(curl)

FetchContent_Declare(
    IXWebSocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
)
FetchContent_MakeAvailable(IXWebSocket)

set(GGML_CUDA ON) # enables GPU support

FetchContent_Declare(
    llama
    GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
    GIT_TAG b10520
)
FetchContent_MakeAvailable(llama)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIM REQUIRED IMPORTED_TARGET libzim)

add_subdirectory(helper)
add_subdirectory(server)
add_subdirectory(crawler)
add_subdirectory(youtube)
add_subdirectory(external)
add_subdirectory(fun/contexto)
add_subdirectory(fun/wikiRace)
