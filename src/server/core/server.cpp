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
                network_->send(it->second, mcg::protocol::MessageType::WorldSnapshot, payload);
            }
        });
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
        {
            throw;
        }
    }
    catch (const std::exception& e)
    {
        if (port_ != 0)
        {
            MCG_WARN("Failed to bind to port {}: {}", port_, e.what());
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
        // Õ‡È‰Ë handle_ping Ë Á‡ÏÂÌË ÛÒÚ‡ÌÓ‚ÍÛ Z:
        auto player = std::make_shared<Player>(player_id);
        player->attributes().set("pos_z", world_.get_surface_z(0, 0));

        players_[player_id] = player;
        sessions_[player_id] = session;
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

    // ƒŒ¡¿¬À≈ÕŒ: ÓÚÎ‡‰Ó˜Ì˚È ÎÓ„
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
        MCG_WARN("Command not found: {}", cmd_name); // ƒŒ¡¿¬À≈ÕŒ
        return;
    }

    std::vector<std::string> args;
    std::string arg;
    while (iss >> arg)
        args.push_back(arg);

    auto& player = players_[session->id()];
    if (cmd->on_command.valid())
    {
        MCG_INFO("Executing Lua command /{}", cmd_name); // ƒŒ¡¿¬À≈ÕŒ
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