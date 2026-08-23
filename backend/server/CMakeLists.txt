add_executable(server
    main.cpp
)

# link to executable
target_include_directories(server PRIVATE
    ${PROJECT_SOURCE_DIR}/external/json/include
    ${INSTALL_DIR}/include
    ${PROJECT_SOURCE_DIR}/secrets
    ${PROJECT_SOURCE_DIR}/helper
)

target_link_libraries(server PRIVATE
    helper
    uwebsockets
    usockets
    z
    pthread
    pqxx
    ctemplate
    llama
)
