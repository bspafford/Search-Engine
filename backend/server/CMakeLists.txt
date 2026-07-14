add_executable(server
    main.cpp
)

# linke to executable
target_include_directories(server PRIVATE

)

target_link_libraries(server PRIVATE
    uwebsockets
    usockets
    z
    pthread
    pqxx
)
