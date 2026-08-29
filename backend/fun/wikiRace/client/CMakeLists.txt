cmake_minimum_required(VERSION 3.16)
project(SearchEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(SKIP_BUILD_TEST_SUITE ON CACHE BOOL "" FORCE)

# ============================================================
# Local libzim
# ============================================================

set(LIBZIM_ROOT
    "${CMAKE_SOURCE_DIR}/Libraries/libzim_win-x86_64-9.6.0"
)

set(LIBZIM_INCLUDE_DIR
    "${LIBZIM_ROOT}/include"
)

set(LIBZIM_LIBRARY
    "${LIBZIM_ROOT}/lib"
)

# ============================================================
# Lexbor
# ============================================================

set(LEXBOR_BUILD_TESTS OFF CACHE BOOL "Disable Lexbor tests" FORCE)
set(LEXBOR_BUILD_EXAMPLES OFF CACHE BOOL "Disable Lexbor examples" FORCE)
set(LEXBOR_BUILD_TOOLS OFF CACHE BOOL "Disable Lexbor tools" FORCE)

FetchContent_Declare(
    lexbor
    GIT_REPOSITORY https://github.com/lexbor/lexbor
    GIT_TAG v3.0.0
)

FetchContent_MakeAvailable(lexbor)

# ============================================================
# CURL
# ============================================================

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

# ============================================================
# llama.cpp / CUDA
# ============================================================

set(GGML_CUDA ON CACHE BOOL "Enable CUDA" FORCE)

FetchContent_Declare(
    llama
    GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
    GIT_TAG b10520
)

FetchContent_MakeAvailable(llama)

# ============================================================
# Application
# ============================================================

add_executable(SearchEngine
    main.cpp
    Parser.cpp
    Database.cpp
    Helper.cpp
    ThreadPool.cpp
    Model.cpp
)

# libzim headers
target_include_directories(SearchEngine PRIVATE
    "${LIBZIM_INCLUDE_DIR}"
    "${CMAKE_SOURCE_DIR}/Libraries/json-develop/include"
    "${CMAKE_SOURCE_DIR}/Libraries/umappp-master/include"
)

# libzim library directory
target_link_directories(SearchEngine PRIVATE
    "${LIBZIM_LIBRARY}"
)

target_link_libraries(SearchEngine PRIVATE
    lexbor
    llama
    ggml
    zim
    CURL::libcurl
)