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

# --- sol2 & Lua ---
FetchContent_Declare(sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG        v3.3.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(sol2)

FetchContent_Declare(lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG        v5.4.6
    GIT_SHALLOW    TRUE)

FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    FetchContent_Populate(lua)
    file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/*.c)
    
    # Исключаем интерпретатор, компилятор и onelua.c
    list(REMOVE_ITEM LUA_SOURCES 
        ${lua_SOURCE_DIR}/lua.c 
        ${lua_SOURCE_DIR}/luac.c
        ${lua_SOURCE_DIR}/onelua.c)
        
    add_library(lua_lib STATIC ${LUA_SOURCES})
    target_include_directories(lua_lib PUBLIC ${lua_SOURCE_DIR})
    
    if(WIN32)
        # На Windows ничего дополнительно не нужно
    else()
        target_compile_definitions(lua_lib PRIVATE LUA_USE_POSIX LUA_USE_DLOPEN)
        target_link_libraries(lua_lib PUBLIC m ${CMAKE_DL_LIBS})
    endif()
endif()

# --- Asio (Standalone) ---
FetchContent_Declare(asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2
    GIT_SHALLOW    TRUE)

FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
    FetchContent_Populate(asio)
endif()

add_library(asio_standalone INTERFACE)
target_include_directories(asio_standalone INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(asio_standalone INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
if(WIN32)
    target_link_libraries(asio_standalone INTERFACE ws2_32 wsock32)
endif()
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(asio_standalone INTERFACE Threads::Threads)
endif()

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