#include "server/lua/lua_engine.h"
#include "server/player/player.h"
#include "server/world/fov.h"
#include "shared/util/logging.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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
    lua_.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::os,
                        sol::lib::math);

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

    api.set_function("is_walkable",
                     [this](int x, int y, int z) { return world_.is_walkable(x, y, z); });

    api.set_function("noise2d",
                     [this](double x, double y) { return world_.get_perlin().noise2d(x, y); });
    api.set_function("noise3d", [this](double x, double y, double z)
                     { return world_.get_perlin().noise3d(x, y, z); });

    api.set_function("set_terrain",
                     [this](int x, int y, int z, std::string sym, std::string color, bool walkable,
                            bool transparent)
                     {
                         Tile t;
                         t.symbol = sym;
                         t.color_hex = color;
                         t.walkable = walkable;
                         t.transparent = transparent;
                         world_.set_terrain(x, y, z, t);
                     });

    api.set_function(
        "set_overlay",
        [this](int x, int y, int z, std::string sym, std::string color, bool transparent)
        {
            Tile t;
            t.symbol = sym;
            t.color_hex = color;
            t.walkable = false;
            t.transparent = transparent;
            world_.set_overlay(x, y, z, t);
        });

    world_.set_generator_callback([this](int cx, int cy, int cz)
                                  { this->on_generate_chunk(cx, cy, cz); });

    // ВАЖНО: вызываем все зарегистрированные декораторы
    world_.set_decorator_callback(
        [this](int cx, int cy, int cz)
        {
            for (auto& fn : decorators_)
            {
                if (!fn.valid()) continue;
                try
                {
                    auto result = fn(cx, cy, cz);
                    if (!result.valid())
                    {
                        sol::error err = result;
                        MCG_ERROR("Decorator error: {}", err.what());
                    }
                }
                catch (const std::exception& e)
                {
                    MCG_ERROR("Decorator C++ exception: {}", e.what());
                }
            }
        });

    api.set_function(
        "update_fov",
        [this](Player& p)
        {
            try
            {
                int px = p.attributes().get("pos_x").get<int>();
                int py = p.attributes().get("pos_y").get<int>();
                int pz = p.attributes().get("pos_z").get<int>();
                int radius = 8;
                int render_depth = 10;
                int ceiling_height = 20;
                int max_step_up = 1;

                auto js_depth = p.attributes().get("render_depth");
                if (js_depth.is_number_integer()) render_depth = js_depth.get<int>();

                auto js_ceil = p.attributes().get("ceiling_height");
                if (js_ceil.is_number_integer()) ceiling_height = js_ceil.get<int>();

                auto js_step = p.attributes().get("max_step_up");
                if (js_step.is_number_integer()) max_step_up = js_step.get<int>();

                // Один параметр для света и тьмы (для симметрии)
                float light_step = 1.25f;
                auto js_ls = p.attributes().get("light_step");
                if (js_ls.is_number()) light_step = js_ls.get<float>();

                std::string render_mode = "auto";
                auto js_mode = p.attributes().get("render_mode");
                if (js_mode.is_string()) render_mode = js_mode.get<std::string>();

                bool topdown = false;
                if (render_mode == "topdown")
                {
                    topdown = true;
                }
                else if (render_mode == "level")
                {
                    topdown = false;
                }
                else
                {
                    bool has_ceiling = false;
                    for (int z = pz + 1; z <= pz + 5; ++z)
                    {
                        if (!world_.is_transparent(px, py, z))
                        {
                            has_ceiling = true;
                            break;
                        }
                    }
                    topdown = !has_ceiling;
                }

                auto adjust_brightness = [](const std::string& hex, double factor) -> std::string
                {
                    if (hex.length() != 6) return hex;
                    try
                    {
                        int r = std::stoi(hex.substr(0, 2), nullptr, 16);
                        int g = std::stoi(hex.substr(2, 2), nullptr, 16);
                        int b = std::stoi(hex.substr(4, 2), nullptr, 16);

                        if (factor >= 1.0)
                        {
                            // ОСВЕТЛЕНИЕ: смешиваем с белым
                            double blend = std::min(factor - 1.0, 1.0);
                            r = r + static_cast<int>((255 - r) * blend);
                            g = g + static_cast<int>((255 - g) * blend);
                            b = b + static_cast<int>((255 - b) * blend);
                        }
                        else
                        {
                            // ЗАТЕМНЕНИЕ: умножаем
                            r = static_cast<int>(r * factor);
                            g = static_cast<int>(g * factor);
                            b = static_cast<int>(b * factor);
                        }

                        r = std::max(0, std::min(255, r));
                        g = std::max(0, std::min(255, g));
                        b = std::max(0, std::min(255, b));
                        char buf[8];
                        snprintf(buf, sizeof(buf), "%02X%02X%02X", r, g, b);
                        return std::string(buf);
                    }
                    catch (...)
                    {
                        return hex;
                    }
                };

                nlohmann::json grid = nlohmann::json::array();
                for (int y = py - radius; y <= py + radius; ++y)
                {
                    std::string row;
                    for (int x = px - radius; x <= px + radius; ++x)
                    {
                        // 1. Туман войны
                        bool visible = true;
                        if (world_.fog_enabled() && (x != px || y != py))
                        {
                            int cx = px, cy = py;
                            int dx = std::abs(x - px), sx = px < x ? 1 : -1;
                            int dy = std::abs(y - py), sy = py < y ? 1 : -1;
                            int err = dx - dy;
                            while (cx != x || cy != y)
                            {
                                if (cx != px || cy != py)
                                {
                                    if (world_.is_column_blocked(cx, cy, pz - 1, pz + 1))
                                    {
                                        visible = false;
                                        break;
                                    }
                                }
                                int e2 = 2 * err;
                                if (e2 > -dy)
                                {
                                    err -= dy;
                                    cx += sx;
                                }
                                if (e2 < dx)
                                {
                                    err += dx;
                                    cy += sy;
                                }
                            }
                        }

                        if (!visible)
                        {
                            row += " ";
                            continue;
                        }

                    // 2. Игрок
                        if (x == px && y == py)
                        {
                            std::string bg = "000000";
                            bool found = false;
                            // Проверяем уровень игрока (может быть в траве)
                            if (world_.has_overlay(x, y, pz))
                            {
                                Tile ov = world_.get_overlay(x, y, pz);
                                if (!ov.symbol.empty())
                                {
                                    bg = ov.color_hex;
                                    found = true;
                                }
                            }
                            if (!found)
                            {
                                Tile tr = world_.get_terrain(x, y, pz);
                                if (!tr.symbol.empty())
                                {
                                    bg = tr.color_hex;
                                    found = true;
                                }
                            }
                            if (!found)
                            {
                                // Игрок в воздухе — смотрим вниз
                                for (int z = pz - 1; z >= pz - 10; --z)
                                {
                                    bool b_found = false;
                                    if (world_.has_overlay(x, y, z))
                                    {
                                        Tile ov = world_.get_overlay(x, y, z);
                                        if (!ov.symbol.empty())
                                        {
                                            bg = ov.color_hex;
                                            b_found = true;
                                        }
                                    }
                                    if (!b_found)
                                    {
                                        Tile tr = world_.get_terrain(x, y, z);
                                        if (!tr.symbol.empty())
                                        {
                                            bg = tr.color_hex;
                                            b_found = true;
                                        }
                                    }
                                    if (b_found)
                                    {
                                        // ИСПРАВЛЕНО: затемняем, если пол ниже игрока
                                        int dist = (pz - 1) - z;
                                        if (dist > 0)
                                        {
                                            bg = adjust_brightness(
                                                bg, std::pow(1.0 / light_step, dist));
                                        }
                                        break;
                                    }
                                }
                            }
                            row += "[cFFD700][bg" + bg + "]@[/bg][/c]";
                            continue;
                        }

                        // 3. Проверка: блокирует ли эта колонна движение игрока?
                        bool blocks_movement = false;
                        if (!world_.is_walkable(x, y, pz))
                        {
                            // Стена на уровне игрока. Может ли он взобраться?
                            bool can_climb = false;
                            for (int step = 1; step <= max_step_up; ++step)
                            {
                                if (world_.is_walkable(x, y, pz + step) &&
                                    world_.is_walkable(x, y, pz + step + 1))
                                {
                                    can_climb = true;
                                    break;
                                }
                            }
                            if (!can_climb) blocks_movement = true;
                        }

                        if (blocks_movement)
                        {
                            // Находим цвет стены
                            std::string wall_color = "808080";
                            if (world_.has_overlay(x, y, pz))
                            {
                                Tile ov = world_.get_overlay(x, y, pz);
                                if (!ov.symbol.empty()) wall_color = ov.color_hex;
                            }
                            else
                            {
                                Tile tr = world_.get_terrain(x, y, pz);
                                if (!tr.symbol.empty()) wall_color = tr.color_hex;
                            }
                            row += "[cFF0000][bg" + wall_color + "]X[/bg][/c]";
                            continue;
                        }

                        // 4. Находим верхний видимый блок
                        int start_z = topdown ? (pz + ceiling_height) : pz;
                        int end_z = pz - render_depth;

                        Tile top_tile;
                        int visible_z = -100;
                        for (int z = start_z; z >= end_z; --z)
                        {
                            if (world_.has_overlay(x, y, z))
                            {
                                Tile ov = world_.get_overlay(x, y, z);
                                if (!ov.symbol.empty())
                                {
                                    visible_z = z;
                                    top_tile = ov;
                                    break;
                                }
                            }
                            Tile tr = world_.get_terrain(x, y, z);
                            if (!tr.symbol.empty())
                            {
                                visible_z = z;
                                top_tile = tr;
                                break;
                            }
                        }

                    // 5. Отрисовка
                        if (visible_z != -100)
                        {
                            std::string fg = top_tile.color_hex;
                            std::string bg = "000000";
                            std::string sym = top_tile.symbol;

                            if (!top_tile.bg_color_hex.empty())
                            {
                                bg = top_tile.bg_color_hex;
                            }
                            else if (sym != " ")
                            {
                                for (int z = visible_z - 1; z >= end_z; --z)
                                {
                                    Tile under = world_.get_terrain(x, y, z);
                                    if (!under.symbol.empty())
                                    {
                                        bg = under.color_hex;
                                        break;
                                    }
                                }
                            }
                            else
                            {
                                bg = fg;
                            }

                            // Освещение: для прозрачных оверлеев (цветов) не учитываем их высоту
                            double factor;
                            if (top_tile.transparent && sym != " ")
                            {
                                // Цветок: фон = трава без осветления, символ осветляем
                                factor = 1.0;
                                fg = adjust_brightness(fg, std::pow(light_step, visible_z - pz));
                            }
                            else
                            {
                                factor = std::pow(light_step, static_cast<double>(visible_z - pz));
                                if (sym == " ")
                                    bg = adjust_brightness(bg, factor);
                                else
                                {
                                    fg = adjust_brightness(fg, factor);
                                    bg = adjust_brightness(bg, factor);
                                }
                            }

                            // Оптимизация: белое или чёрное — прекращаем
                            if (factor >= 2.0)
                            {
                                row += "[cFFFFFF][bgFFFFFF] [/bg][/c]";
                            }
                            else if (factor <= 0.05)
                            {
                                row += "[c000000][bg000000] [/bg][/c]";
                            }
                            else if (sym == " ")
                            {
                                row += "[bg" + bg + "] [/bg]";
                            }
                            else
                            {
                                row += "[c" + fg + "][bg" + bg + "]" + sym + "[/bg][/c]";
                            }
                        }
                        else
                        {
                            row += " ";
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
            }
            catch (const std::exception& e)
            {
                MCG_ERROR("Exception in update_fov: {}", e.what());
            }
        });
}

LuaEngine::~LuaEngine() = default;

void LuaEngine::load_decorators(const std::string& biomes_dir)
{
    namespace fs = std::filesystem;
    if (!fs::exists(biomes_dir))
    {
        MCG_WARN("Biomes directory not found: {}", biomes_dir);
        return;
    }

    for (const auto& entry : fs::directory_iterator(biomes_dir))
    {
        if (entry.path().extension() != ".lua") continue;
        try
        {
            auto result = lua_.script_file(entry.path().string());
            if (result.valid() && result.get_type() == sol::type::function)
            {
                decorators_.push_back(result);
                MCG_INFO("Loaded biome decorator: {}", entry.path().filename().string());
            }
            else
            {
                MCG_WARN("Biome file did not return a function: {}",
                         entry.path().filename().string());
            }
        }
        catch (const sol::error& e)
        {
            MCG_ERROR("Failed to load biome {}: {}", entry.path().string(), e.what());
        }
    }
}

void LuaEngine::on_generate_chunk(int cx, int cy, int cz)
{
    sol::protected_function func = lua_["on_generate_chunk"];
    if (!func.valid()) return;
    try
    {
        auto result = func(cx, cy, cz);
        if (!result.valid())
        {
            sol::error err = result;
            MCG_ERROR("Lua error in on_generate_chunk: {}", err.what());
        }
    }
    catch (const std::exception& e)
    {
        MCG_ERROR("C++ exception in on_generate_chunk: {}", e.what());
    }
}

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