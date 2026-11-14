#include "server/lua/lua_engine.h"
#include "server/player/player.h"
#include "shared/util/logging.h"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace mcg::server
{

static sol::object json_to_sol(sol::state_view lua, const nlohmann::json& j)
{
    if (j.is_null()) return sol::nil;
    if (j.is_boolean()) return sol::make_object(lua, j.get<bool>());
    if (j.is_number_integer()) return sol::make_object(lua, j.get<int64_t>());
    if (j.is_number_float()) return sol::make_object(lua, j.get<double>());
    if (j.is_string()) return sol::make_object(lua, j.get<std::string>());
    return sol::nil;
}

LuaEngine::LuaEngine()
{
    lua_.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::os);

    lua_.new_usertype<Player>("Player", "id", &Player::id, "uptime_seconds",
                              &Player::uptime_seconds);

    auto api = lua_["api"].get_or_create<sol::table>();

    api.set_function("get_attr",
                     [](sol::this_state ts, Player& p, const std::string& key) -> sol::object
                     {
                         sol::state_view lua(ts);
                         return json_to_sol(lua, p.attributes().get(key));
                     });

    api.set_function("set_attr",
                     [](Player& p, const std::string& key, sol::object value)
                     {
                         if (value.is<int64_t>())
                             p.attributes().set(key, value.as<int64_t>());
                         else if (value.is<double>())
                             p.attributes().set(key, value.as<double>());
                         else if (value.is<bool>())
                             p.attributes().set(key, value.as<bool>());
                         else if (value.is<std::string>())
                             p.attributes().set(key, value.as<std::string>());
                     });

    api.set_function("broadcast",
                     [this](const std::string& text)
                     {
                         if (broadcast_cb_) broadcast_cb_(text);
                     });
}

LuaEngine::~LuaEngine() = default;

void LuaEngine::run_script(const std::string& path)
{
    try
    {
        lua_.script_file(path);
        MCG_INFO("Lua script loaded: {}", path);
    }
    catch (const sol::error& e)
    {
        MCG_ERROR("Lua error in {}: {}", path, e.what());
    }
}

void LuaEngine::load_commands(const std::string& commands_dir)
{
    namespace fs = std::filesystem;

    if (!fs::exists(commands_dir))
    {
        MCG_WARN("Commands directory does not exist: {}", commands_dir);
        return;
    }

    for (const auto& entry : fs::directory_iterator(commands_dir))
    {
        if (entry.path().extension() != ".lua") continue;

        try
        {
            auto result = lua_.script_file(entry.path().string());

            if (!result.valid() || result.get_type() != sol::type::table)
            {
                MCG_WARN("Script did not return a table: {}", entry.path().string());
                continue;
            }

            sol::table t = result;
            LuaCommand cmd;

            // Безопасное чтение полей
            auto get_str = [](sol::table tbl, const char* key, const std::string& def = "")
            {
                sol::object o = tbl[key];
                return (o.valid() && o.is<std::string>()) ? o.as<std::string>() : def;
            };
            auto get_bool = [](sol::table tbl, const char* key, bool def = false)
            {
                sol::object o = tbl[key];
                return (o.valid() && o.is<bool>()) ? o.as<bool>() : def;
            };

            cmd.name = get_str(t, "name");
            cmd.description = get_str(t, "description");
            cmd.args = get_str(t, "args");
            cmd.category = get_str(t, "category");
            cmd.admin_only = get_bool(t, "admin_only", false);
            cmd.requires_active_player = get_bool(t, "requires_active_player", true);

            sol::object on_cmd = t["on_command"];
            if (on_cmd.valid() && on_cmd.is<sol::protected_function>())
            {
                cmd.on_command = on_cmd.as<sol::protected_function>();
            }

            if (cmd.name.empty())
            {
                MCG_WARN("Command has no name: {}", entry.path().string());
                continue;
            }

            // ИСПРАВЛЕНО: сохраняем имя до перемещения
            const std::string cmd_name = cmd.name;
            commands_[cmd_name] = std::move(cmd);
            MCG_INFO("Registered command: /{}", cmd_name);
        }
        catch (const sol::error& e)
        {
            MCG_ERROR("Failed to load command {}: {}", entry.path().string(), e.what());
        }
    }
}

LuaCommand* LuaEngine::find_command(const std::string& name)
{
    auto it = commands_.find(name);
    if (it == commands_.end()) return nullptr;
    return &it->second;
}

void LuaEngine::on_player_connect(std::shared_ptr<Player> player)
{
    sol::protected_function func = lua_["on_player_connect"];
    if (func.valid())
    {
        auto result = func(player.get());
        if (!result.valid())
        {
            sol::error err = result;
            MCG_ERROR("Lua error in on_player_connect: {}", err.what());
        }
    }
}

} // namespace mcg::server