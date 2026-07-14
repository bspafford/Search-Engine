add_executable(server
    main.cpp
)

# linke to executable
target_include_directories(server PRIVATE
    ${PROJECT_SOURCE_DIR}/external/json/include
    ${INSTALL_DIR}/include
)

target_link_libraries(server PRIVATE
    uwebsockets
    usockets
    z
    pthread
    pqxx
    ctemplate
)
