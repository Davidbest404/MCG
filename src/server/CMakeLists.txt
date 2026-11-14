add_executable(mcg_server
    main.cpp
    core/server.cpp
    network/network_thread.cpp
    network/session.cpp
    lua/lua_engine.cpp
)

target_link_libraries(mcg_server PRIVATE 
    mcg_shared 
    asio_standalone 
    sol2::sol2 
    lua_lib
)

# Копируем папку lua в директорию с бинарником при сборке
add_custom_command(TARGET mcg_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/lua
        $<TARGET_FILE_DIR:mcg_server>/lua
)