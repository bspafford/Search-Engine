add_executable(youtube
    main.cpp
)

target_include_directories(youtube PRIVATE
    ${PROJECT_SOURCE_DIR}/external/json/include
    ${INSTALL_DIR}/include
    ${PROJECT_SOURCE_DIR}/secrets
    ${PROJECT_SOURCE_DIR}/helper
)

target_link_libraries(youtube PRIVATE
    helper
    uwebsockets
    usockets
    z
    pthread
    pqxx
    ctemplate
)
