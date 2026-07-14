add_executable(crawler
    main.cpp
)

# linke to executable
target_include_directories(crawler PRIVATE
    
)

target_link_libraries(crawler PRIVATE
    lexbor
    libcurl
    pqxx
)
