cmake_minimum_required(VERSION 3.24)

project(MCG
    VERSION 0.1.0
    DESCRIPTION "Multi Console Game — extensible MUD engine"
    LANGUAGES CXX)

# ---- Global C++ settings ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8 /EHsc)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---- Options ----
option(MCG_BUILD_TESTS  "Build unit tests"  ON)
option(MCG_BUILD_SERVER "Build server"      ON)
option(MCG_BUILD_CLIENT "Build client"      ON)

# ---- Dependencies ----
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        10.2.1
    GIT_SHALLOW    TRUE)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
    GIT_SHALLOW    TRUE)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(fmt spdlog json)

if(MCG_BUILD_TESTS)
    FetchContent_Declare(Catch2
        URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.2.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(Catch2)
endif()

# ---- Targets ----
add_subdirectory(src/shared)

if(MCG_BUILD_SERVER)
    add_subdirectory(src/server)
endif()

if(MCG_BUILD_CLIENT)
    add_subdirectory(src/client)
endif()

if(MCG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()