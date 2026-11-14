add_executable(mcg_server
    main.cpp
    core/server.cpp
    network/network_thread.cpp
    network/session.cpp
    lua/lua_engine.cpp
    world/world.cpp
    world/fov.cpp
 "world/perlin_noise.h")

target_link_libraries(mcg_server PRIVATE 
    mcg_shared 
    asio_standalone 
    sol2::sol2 
    lua_lib
)