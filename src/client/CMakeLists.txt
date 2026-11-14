add_executable(mcg_client
    main.cpp
    network/connection.cpp
)

target_link_libraries(mcg_client PRIVATE mcg_shared asio_standalone)