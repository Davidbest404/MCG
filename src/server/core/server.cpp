#include "server/core/server.h"
#include "shared/util/error.h"
#include "shared/util/logging.h"
#include <chrono>
#include <system_error>

namespace mcg::server
{

Server::Server(uint16_t port) : port_(port) {}

Server::~Server()
{
    stop();
}

void Server::run()
{
    running_ = true;

    // Инициализация Lua и загрузка тестового скрипта
    lua_ = std::make_unique<LuaEngine>();
    lua_->run_script("lua/actions/commands/test.lua");

    try
    {
        network_ = std::make_unique<NetworkThread>(port_);
    }
    catch (const std::system_error& e)
    {
        if (port_ != 0)
        {
            MCG_WARN("Failed to bind to port {}: {}", port_, mcg::util::error_message(e.code()));
            MCG_WARN("Trying any free port instead...");
            network_ = std::make_unique<NetworkThread>(0);
        }
        else
        {
            throw;
        }
    }
    catch (const std::exception& e)
    {
        if (port_ != 0)
        {
            MCG_WARN("Failed to bind to port {}: {}", port_, e.what());
            MCG_WARN("Trying any free port instead...");
            network_ = std::make_unique<NetworkThread>(0);
        }
        else
        {
            throw;
        }
    }

    MCG_INFO("Server is listening on port {}", network_->port());
    network_->start();

    logic_thread_ = std::thread([this]() { logic_loop(); });
    logic_thread_.join();
}

void Server::stop()
{
    running_ = false;
    if (network_) network_->stop();
    if (logic_thread_.joinable()) logic_thread_.join();
}

void Server::logic_loop()
{
    MCG_INFO("Logic thread started");
    NetworkThread::IncomingMessage msg;

    while (running_)
    {
        while (network_->pop_message(msg))
        {
            auto [session, type, payload] = msg;

            if (type == mcg::protocol::MessageType::Ping)
            {
                MCG_INFO("Received Ping from session {}", session->id());

                // Создаём игрока, если его нет
                uint64_t player_id = session->id();
                if (players_.find(player_id) == players_.end())
                {
                    auto player = std::make_shared<Player>(player_id);
                    players_[player_id] = player;

                    MCG_INFO("Player {} created. HP before Lua: {}", player_id,
                             player->attributes().get("hp").dump());

                    // Вызываем Lua-хук
                    lua_->on_player_connect(player);

                    MCG_INFO("Player {} HP after Lua: {}", player_id,
                             player->attributes().get("hp").dump());
                }

                // Отправляем Pong с обновленным HP
                auto& p = players_[player_id];
                nlohmann::json reply = {{"reply", "hello from server"},
                                        {"hp", p->attributes().get("hp")}};
                network_->send(session, mcg::protocol::MessageType::Pong, reply);
            }
            else
            {
                MCG_INFO("Received msg type {} from session {}", static_cast<int>(type),
                         session->id());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace mcg::server