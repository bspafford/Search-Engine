add_executable(wikiRace
    main.cpp
)

# linke to executable
target_include_directories(wikiRace PRIVATE
    ${PROJECT_SOURCE_DIR}/external/json/include
    ${INSTALL_DIR}/include
    ${PROJECT_SOURCE_DIR}/secrets
    ${PROJECT_SOURCE_DIR}/helper
)

target_link_libraries(wikiRace PRIVATE
    lexbor
    pqxx
    helper
    llama
)
