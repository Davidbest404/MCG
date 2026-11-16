#include "server/core/server.h"
#include "shared/util/error.h"
#include "shared/util/logging.h"
#include <chrono>
#include <sstream>
#include <system_error>

namespace mcg::server
{

Server::Server(uint16_t port, uint32_t seed, bool fog_enabled)
    : port_(port), world_(seed, fog_enabled)
{
}

Server::~Server()
{
    stop();
}

void Server::run()
{
    running_ = true;

    lua_ = std::make_unique<LuaEngine>(world_);
    lua_->set_broadcast_callback([this](const std::string& text) { broadcast(text); });
    lua_->set_map_update_callback(
        [this](uint64_t player_id, const nlohmann::json& payload)
        {
            auto it = sessions_.find(player_id);
            if (it != sessions_.end())
            {
                network_->send(it->second, mcg::protocol::MessageType::WorldRender, payload);
            }
        });

    lua_->set_entity_callback(
        [this](int x, int y, int z) -> std::tuple<std::string, std::string>
        {
            for (auto& [id, p] : players_)
            {
                if (p->attributes().get("pos_x").get<int>() == x &&
                    p->attributes().get("pos_y").get<int>() == y &&
                    p->attributes().get("pos_z").get<int>() == z)
                {
                    std::string sym = "@";
                    std::string col = "FFD700";
                    auto js_sym = p->attributes().get("symbol");
                    if (js_sym.is_string()) sym = js_sym.get<std::string>();
                    auto js_col = p->attributes().get("color");
                    if (js_col.is_string()) col = js_col.get<std::string>();
                    return {sym, col};
                }
            }
            return {"", ""};
        });

    // 1. Общие функции генерации
    lua_->run_script("lua/world/common.lua");
    // 2. Базовый ландшафт
    lua_->run_script("lua/world/generator.lua");
    // 3. Биомы
    lua_->load_decorators("lua/world/biomes");
    // 4. Расы
    lua_->load_races("lua/races");
    // 5. Команды
    lua_->load_commands("lua/actions/commands");

    try
    {
        network_ = std::make_unique<NetworkThread>(port_);
    }
    catch (const std::system_error& e)
    {
        if (port_ != 0)
        {
            MCG_WARN("Failed to bind to port {}: {}", port_, mcg::util::error_message(e.code()));
            network_ = std::make_unique<NetworkThread>(0);
        }
        else
            throw;
    }
    catch (const std::exception& e)
    {
        if (port_ != 0)
        {
            MCG_WARN("Failed to bind to port {}: {}", port_, e.what());
            network_ = std::make_unique<NetworkThread>(0);
        }
        else
            throw;
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

void Server::broadcast(const std::string& text)
{
    nlohmann::json msg = {{"text", text}, {"from", "System"}, {"scope", "global"}};
    for (auto& [id, session] : sessions_)
    {
        network_->send(session, mcg::protocol::MessageType::ChatMessage, msg);
    }
}

void Server::handle_ping(std::shared_ptr<Session> session, const nlohmann::json& /*payload*/)
{
    uint64_t player_id = session->id();

    if (players_.find(player_id) == players_.end())
    {
        auto player = std::make_shared<Player>(player_id);
        player->attributes().set("pos_z", world_.get_surface_z(0, 0));

        players_[player_id] = player;
        sessions_[player_id] = session;

        // Выдаём базовую расу
        auto base_it = lua_->races().find("base");
        if (base_it != lua_->races().end())
        {
            for (auto& [k, v] : base_it->second.items())
            {
                if (v.is_string())
                    player->attributes().set(k, v.get<std::string>());
                else if (v.is_number_integer())
                    player->attributes().set(k, v.get<int64_t>());
                else if (v.is_number_float())
                    player->attributes().set(k, v.get<double>());
                else if (v.is_boolean())
                    player->attributes().set(k, v.get<bool>());
            }
            player->attributes().set("race", "base");
        }

        lua_->on_player_connect(player);
        broadcast("Player " + player->attributes().get("name").get<std::string>() + " joined");
    }

    nlohmann::json reply = {{"reply", "pong"}, {"hp", players_[player_id]->attributes().get("hp")}};
    network_->send(session, mcg::protocol::MessageType::Pong, reply);
}

void Server::handle_client_message(std::shared_ptr<Session> session, const nlohmann::json& payload)
{
    uint64_t player_id = session->id();
    if (players_.find(player_id) == players_.end()) return;

    std::string text = payload.value("text", "");
    if (text.empty()) return;

    MCG_INFO("Received text from player {}: '{}'", player_id, text);

    if (text[0] == '/')
    {
        handle_command(session, text);
    }
    else
    {
        std::string name = players_[player_id]->attributes().get("name").get<std::string>();
        nlohmann::json msg = {{"text", text}, {"from", name}, {"scope", "global"}};
        for (auto& [id, s] : sessions_)
        {
            network_->send(s, mcg::protocol::MessageType::ChatMessage, msg);
        }
    }
}

void Server::handle_command(std::shared_ptr<Session> session, const std::string& text)
{
    std::string cmd_line = text.substr(1);
    std::istringstream iss(cmd_line);
    std::string cmd_name;
    iss >> cmd_name;

    if (cmd_name.empty()) return;

    if (cmd_name == "help")
    {
        std::string result = "Available commands:\n";
        for (const auto& [name, cmd] : lua_->commands())
        {
            result += "  /" + name;
            if (!cmd.args.empty()) result += " " + cmd.args;
            result += " - " + cmd.description + "\n";
        }
        nlohmann::json msg = {{"text", result}, {"from", "System"}, {"scope", "direct"}};
        network_->send(session, mcg::protocol::MessageType::ChatMessage, msg);
        return;
    }

    LuaCommand* cmd = lua_->find_command(cmd_name);
    if (!cmd)
    {
        MCG_WARN("Command not found: {}", cmd_name);
        return;
    }

    std::vector<std::string> args;
    std::string arg;
    while (iss >> arg)
        args.push_back(arg);

    auto& player = players_[session->id()];
    if (cmd->on_command.valid())
    {
        MCG_INFO("Executing Lua command /{}", cmd_name);
        auto result = cmd->on_command(player.get(), args);
        if (!result.valid())
        {
            sol::error err = result;
            MCG_ERROR("Lua command /{} failed: {}", cmd_name, err.what());
        }
    }
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
                handle_ping(session, payload);
            else if (type == mcg::protocol::MessageType::ChatMessage)
                handle_client_message(session, payload);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace mcg::server