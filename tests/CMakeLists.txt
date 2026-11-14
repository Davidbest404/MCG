add_executable(mcg_tests
    test_main.cpp
)

target_link_libraries(mcg_tests PRIVATE
    mcg_shared
    Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

catch_discover_tests(mcg_tests)