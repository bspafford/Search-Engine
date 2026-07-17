add_executable(crawler
    main.cpp
    renderJS.cpp
)

# linke to executable
target_include_directories(crawler PRIVATE
    ${PROJECT_SOURCE_DIR}/external/json/include
    ${INSTALL_DIR}/include
)

target_link_libraries(crawler PRIVATE
    lexbor
    libcurl
    pqxx
    ixwebsocket
)
