#pragma once

#include <functional>
#include <memory>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>

namespace mcg::server
{

class Player;

// Описание Lua-команды
struct LuaCommand
{
    std::string name;
    std::string description;
    std::string args;
    std::string category;
    bool admin_only = false;
    bool requires_active_player = true;
    sol::protected_function on_command;
};

class LuaEngine
{
public:
    LuaEngine();
    ~LuaEngine();

    void run_script(const std::string& path);
    void on_player_connect(std::shared_ptr<Player> player);

    // Сканирует папку lua/actions/commands/ и загружает все команды
    void load_commands(const std::string& commands_dir);

    // Возвращает команду по имени, если она существует
    LuaCommand* find_command(const std::string& name);

    // Возвращает все загруженные команды
    const std::unordered_map<std::string, LuaCommand>& commands() const { return commands_; }

    // Устанавливает callback для broadcast-сообщений (например, сервер будет их рассылать)
    using BroadcastCallback = std::function<void(const std::string& text)>;
    void set_broadcast_callback(BroadcastCallback cb) { broadcast_cb_ = std::move(cb); }

private:
    sol::state lua_;
    std::unordered_map<std::string, LuaCommand> commands_;
    BroadcastCallback broadcast_cb_;
};

} // namespace mcg::server