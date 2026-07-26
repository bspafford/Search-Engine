add_executable(wikiRace
    main.cpp
)

# linke to executable
target_include_directories(wikiRace PRIVATE
    ${PROJECT_SOURCE_DIR}/secrets
)

target_link_libraries(wikiRace PRIVATE
    lexbor
    pqxx
)
