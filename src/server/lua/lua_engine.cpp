#include "server/lua/lua_engine.h"
#include "server/player/player.h"
#include "server/world/fov.h"
#include "shared/util/logging.h"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

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

LuaEngine::LuaEngine(World& world) : world_(world)
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

        // API: проверить, можно ли пройти (теперь с Z)
    api.set_function("is_walkable",
                     [this](int x, int y, int z) { return world_.is_walkable(x, y, z); });

    // API: обновить FOV и отправить карту клиенту
    api.set_function("update_fov",
                     [this](Player& p)
                     {
                         int px = p.attributes().get("pos_x").get<int>();
                         int py = p.attributes().get("pos_y").get<int>();
                         int pz = p.attributes().get("pos_z").get<int>();
                         int radius = 10; // –адиус обзора

                         nlohmann::json grid = nlohmann::json::array();

                         for (int y = py - radius; y <= py + radius; ++y)
                         {
                             std::string row;
                             for (int x = px - radius; x <= px + radius; ++x)
                             {
                                 if (x == px && y == py)
                                 {
                                     // »грок в центре Ч всегда €ркий
                                     row += "[cE]@[/c]";
                                     continue;
                                 }

                                 if (!world_.fog_enabled() ||
                                     fov::has_los(world_, px, py, pz, x, y, pz))
                                 {
                                     Tile t = world_.get_tile(x, y, pz);
                                     char sym = t.symbol;

                                     // ¬ыбираем цвет в зависимости от символа и высоты (z)
                                     if (sym == '#')
                                     {
                                         row += "[c8]#[/c]"; // —тены Ч темно-серые
                                     }
                                     else if (sym == '^')
                                     {
                                         row += "[cF]^[/c]"; // √оры Ч белые
                                     }
                                     else if (sym == '~')
                                     {
                                         row += "[c1]~[/c]"; // ¬ода Ч син€€
                                     }
                                     else if (sym == '"')
                                     {
                                         row += "[c2]\"[/c]"; // “рава Ч зелена€
                                     }
                                     else
                                     {
                                         // ѕуста€ земл€. √радиент по Z
                                         if (pz < 0)
                                             row += "[c4].[/c]"; // √лубоко Ч темно-красный
                                         else if (pz == 0)
                                             row += "[c7].[/c]"; // ѕоверхность Ч светло-серый
                                         else
                                             row += "[cC].[/c]"; // ¬ысоко Ч голубой
                                     }
                                 }
                                 else
                                 {
                                     row += " "; // “уман войны (пустота)
                                 }
                             }
                             grid.push_back(row);
                         }

                         if (map_update_cb_)
                         {
                             nlohmann::json payload;
                             payload["x"] = px;
                             payload["y"] = py;
                             payload["z"] = pz;
                             payload["grid"] = grid;
                             map_update_cb_(p.id(), payload);
                         }
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
    if (!fs::exists(commands_dir)) return;

    for (const auto& entry : fs::directory_iterator(commands_dir))
    {
        if (entry.path().extension() != ".lua") continue;

        try
        {
            auto result = lua_.script_file(entry.path().string());
            if (!result.valid() || result.get_type() != sol::type::table) continue;

            sol::table t = result;
            LuaCommand cmd;

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

            if (cmd.name.empty()) continue;

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