add_library(helper
    helper.cpp
)

target_include_directories(helper PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/external/json/include
    ${INSTALL_DIR}/include
)

target_link_libraries(helper PRIVATE
    llama
)
