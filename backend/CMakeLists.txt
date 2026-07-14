cmake_minimum_required(VERSION 3.16)
project(SearchEngine)

set(CMAKE_CXX_STANDARD 20) # c++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# symlink or copy the file to root so Neovim/clangd sees it
add_custom_target(copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json"
    COMMENT "Exposing FetchContent headers to Neovim"
)

include(FetchContent)
# include(ExternalProject)

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
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_16_0
)
FetchContent_MakeAvailable(curl)

# External Projects
# ExternalProject_Add(
    # ctemplate
    # GIT_REPOSITORY https://github.com/olafvdspek/ctemplate.git
    # GIT_TAG ctemplate-2.4

    # BUILD_IN_SOURCE 1

    # PATCH_COMMAND sh <SOURCE_DIR>/autogen.sh

    # CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=<INSTALL_DIR>

    # BUILD_COMMAND $(MAKE)
    # INSTALL_COMMAND $(MAKE) install
# )


add_subdirectory(server)
add_subdirectory(crawler)
add_subdirectory(external)
