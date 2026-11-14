add_executable(mcg_tests
    test_main.cpp
    test_math.cpp
    test_attributes.cpp
    test_color.cpp
    test_protocol.cpp
    test_lockfree_queue.cpp
)

target_link_libraries(mcg_tests PRIVATE
    mcg_shared
    Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

catch_discover_tests(mcg_tests)