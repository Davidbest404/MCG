#pragma once

#include "server/world/world.h"
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mcg::server
{

class Player;

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
    LuaEngine(World& world);
    ~LuaEngine();

    void run_script(const std::string& path);
    void on_player_connect(std::shared_ptr<Player> player);
    void on_generate_chunk(int cx, int cy, int cz);
    void load_commands(const std::string& commands_dir);
    void load_decorators(const std::string& biomes_dir); // мнбне
    LuaCommand* find_command(const std::string& name);
    const std::unordered_map<std::string, LuaCommand>& commands() const { return commands_; }

    using BroadcastCallback = std::function<void(const std::string& text)>;
    void set_broadcast_callback(BroadcastCallback cb) { broadcast_cb_ = std::move(cb); }

    using MapUpdateCallback =
        std::function<void(uint64_t player_id, const nlohmann::json& payload)>;
    void set_map_update_callback(MapUpdateCallback cb) { map_update_cb_ = std::move(cb); }

private:
    sol::state lua_;
    World& world_;
    std::unordered_map<std::string, LuaCommand> commands_;
    std::vector<sol::protected_function> decorators_; // мнбне
    BroadcastCallback broadcast_cb_;
    MapUpdateCallback map_update_cb_;
};

} // namespace mcg::server