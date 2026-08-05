add_executable(crawler
    main.cpp
    Renderer.cpp
    UrlHelper.cpp
    Indexer.cpp
    ThreadPool.cpp
    Fetcher.cpp
    Database.cpp
    Parser.cpp
)

# linke to executable
target_include_directories(crawler PRIVATE
    ${PROJECT_SOURCE_DIR}/external/json/include
    ${INSTALL_DIR}/include
    ${PROJECT_SOURCE_DIR}/secrets
    ${PROJECT_SOURCE_DIR}/helper
)

find_package(Boost REQUIRED COMPONENTS url)

target_link_libraries(crawler PRIVATE
    helper
    lexbor
    libcurl
    pqxx
    ixwebsocket
    Boost::url
    llama
)
