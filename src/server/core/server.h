#pragma once

#include "server/lua/lua_engine.h"
#include "server/network/network_thread.h"
#include "server/player/player.h"
#include "server/world/world.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mcg::server
{

class Server
{
public:
    // ИЗМЕНЕНО: добавлены seed и fog_enabled
    Server(uint16_t port, uint32_t seed, bool fog_enabled);
    ~Server();

    void run();
    void stop();

private:
    void logic_loop();
    void handle_ping(std::shared_ptr<Session> session, const nlohmann::json& payload);
    void handle_client_message(std::shared_ptr<Session> session, const nlohmann::json& payload);
    void handle_command(std::shared_ptr<Session> session, const std::string& text);
    void broadcast(const std::string& text);

    uint16_t port_;
    World world_; // <-- Здесь хранится сгенерированный мир
    std::unique_ptr<NetworkThread> network_;
    std::unique_ptr<LuaEngine> lua_;
    std::unordered_map<uint64_t, std::shared_ptr<Player>> players_;
    std::unordered_map<uint64_t, std::shared_ptr<Session>> sessions_;

    std::thread logic_thread_;
    std::atomic<bool> running_{false};
};

} // namespace mcg::server