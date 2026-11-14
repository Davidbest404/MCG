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
    // Замени только update_fov:
    api.set_function(
        "update_fov",
        [this](Player& p)
        {
            int px = p.attributes().get("pos_x").get<int>();
            int py = p.attributes().get("pos_y").get<int>();
            int pz = p.attributes().get("pos_z").get<int>();
            int radius = 7;

            nlohmann::json grid = nlohmann::json::array();
            for (int y = py - radius; y <= py + radius; ++y)
            {
                std::string row;
                for (int x = px - radius; x <= px + radius; ++x)
                {
                    if (!world_.fog_enabled() || fov::has_los(world_, px, py, pz, x, y, pz))
                    {
                        if (x == px && y == py)
                        {
                            // --- ИГРОК ---
                            // Определяем цвет блока под ногами игрока
                            std::string bg_color = "0"; // По умолчанию пустота
                            int gz = pz - 1;
                            bool has_ov = world_.has_overlay(x, y, gz);
                            Tile overlay = world_.get_overlay(x, y, gz);

                            if (has_ov && overlay.symbol != ' ')
                            {
                                if (overlay.symbol == '~')
                                    bg_color = "1"; // Вода - синий фон
                                else if (overlay.symbol == '"')
                                    bg_color = "2"; // Трава - зеленый фон
                                else if (overlay.symbol == '*')
                                    bg_color = "F"; // Снег - белый фон
                            }
                            else
                            {
                                Tile terrain = world_.get_terrain(x, y, gz);
                                if (terrain.symbol == '#') bg_color = "7"; // Камень - серый фон
                            }
                            row += "[cE][bg" + bg_color + "]@[/bg][/c]";
                            continue;
                        }

                        if (!world_.is_walkable(x, y, pz))
                        {
                            // --- СТЕНА НА УРОВНЕ ИГРОКА ---
                            // Черный крест на сером фоне (цвет камня)
                            row += "[c0][bg7]x[/bg][/c]";
                            continue;
                        }

                        // Ищем пол внизу
                        int top_z = -100;
                        for (int z = pz - 1; z >= pz - 20; --z)
                        {
                            if (!world_.is_walkable(x, y, z))
                            {
                                top_z = z;
                                break;
                            }
                        }

                        if (top_z != -100)
                        {
                            int depth = pz - (top_z + 1);

                            bool has_ov = world_.has_overlay(x, y, top_z);
                            Tile overlay = world_.get_overlay(x, y, top_z);

                            // Определяем цвет фона (цвет пола на дне)
                            std::string bg_color = "7"; // По умолчанию камень
                            char floor_sym = '#';
                            if (has_ov && overlay.symbol != ' ')
                            {
                                floor_sym = overlay.symbol;
                                if (floor_sym == '~')
                                    bg_color = "1"; // Вода
                                else if (floor_sym == '"')
                                    bg_color = "2"; // Трава
                                else if (floor_sym == '*')
                                    bg_color = "F"; // Снег
                            }

                            if (depth == 0)
                            {
                                // --- РОВНЫЙ ПОЛ ---
                                row += "[c" + bg_color + "]";
                                if (floor_sym == '#')
                                    row += "█";
                                else
                                    row += std::string(1, floor_sym);
                                row += "[/c]";
                            }
                            else
                            {
                                // --- ГЛУБИНА ---
                                // Используем градиентные символы, цвет фона = цвет пола
                                std::string fg = "0"; // Черный
                                if (depth > 5)
                                    fg = "0"; // Очень глубоко - черный
                                else if (depth > 2)
                                    fg = "8"; // Средняя глубина - темно-серый

                                std::string sym = "░";
                                if (depth == 1)
                                    sym = "▓";
                                else if (depth == 2)
                                    sym = "▒";

                                row += "[c" + fg + "][bg" + bg_color + "]" + sym + "[/bg][/c]";
                            }
                        }
                        else
                        {
                            // --- БЕЗДНА ---
                            row += "[c0][bg0]░[/bg][/c]";
                        }
                    }
                    else
                    {
                        row += " "; // Туман войны
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