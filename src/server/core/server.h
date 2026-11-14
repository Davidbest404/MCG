#pragma once

#include "server/lua/lua_engine.h"
#include "server/network/network_thread.h"
#include "server/player/player.h"
#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>

namespace mcg::server
{

class Server
{
public:
    Server(uint16_t port);
    ~Server();

    void run();
    void stop();

private:
    void logic_loop();

    uint16_t port_;
    std::unique_ptr<NetworkThread> network_;
    std::unique_ptr<LuaEngine> lua_;
    std::unordered_map<uint64_t, std::shared_ptr<Player>> players_;

    std::thread logic_thread_;
    std::atomic<bool> running_{false};
};

} // namespace mcg::server