add_library(mcg_shared STATIC
    util/logging.cpp
)

target_include_directories(mcg_shared PUBLIC
    ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(mcg_shared PUBLIC
    fmt::fmt
    spdlog::spdlog
    nlohmann_json::nlohmann_json)