add_executable(mcg_server
    main.cpp
)

target_link_libraries(mcg_server PRIVATE mcg_shared)