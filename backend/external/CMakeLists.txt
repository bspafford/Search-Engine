set(USOCKETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/uWebSockets/uSockets)

file(GLOB USOCKETS_C_SOURCES
    ${USOCKETS_DIR}/src/*.c
    ${USOCKETS_DIR}/src/eventing/*.c
    ${USOCKETS_DIR}/src/crypto/*.c
    ${USOCKETS_DIR}/src/io_uring/*.c
)

file(GLOB USOCKETS_CPP_SOURCES
    ${USOCKETS_DIR}/src/crypto/*.cpp
)

add_library(usockets STATIC
    ${USOCKETS_C_SOURCES}
    ${USOCKETS_CPP_SOURCES}
)

target_include_directories(usockets PUBLIC
    ${USOCKETS_DIR}/src
)

set_target_properties(usockets PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
)

# OpenSSL
find_package(OpenSSL REQUIRED)

target_compile_definitions(usockets PRIVATE
    LIBUS_USE_OPENSSL
)

target_link_libraries(usockets PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
)

# uWebSockets headers
add_library(uwebsockets INTERFACE)

target_include_directories(uwebsockets INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/uWebSockets/src
)
