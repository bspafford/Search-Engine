add_executable(contexto
    contexto.cpp
    bot.cpp
)

# linke to executable
target_include_directories(contexto PRIVATE
    ${PROJECT_SOURCE_DIR}/external/json/include
    ${INSTALL_DIR}/include
)

find_package(Boost REQUIRED COMPONENTS url)

target_link_libraries(contexto PRIVATE
    lexbor
    libcurl
    pqxx
    ixwebsocket
    Boost::url
    llama
)
