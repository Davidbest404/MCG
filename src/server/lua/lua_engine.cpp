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

    // ИСПРАВЛЕНО: параметр walkable стал опциональным (по умолчанию = transparent)
    api.set_function("set_overlay",
                     [this](int x, int y, int z, std::string sym, std::string color,
                            bool transparent, sol::object walkable_obj)
                     {
                         Tile t;
                         t.symbol = sym;
                         t.color_hex = color;
                         t.transparent = transparent;
                         if (walkable_obj.valid() && walkable_obj.is<bool>())
                         {
                             t.walkable = walkable_obj.as<bool>();
                         }
                         else
                         {
                             t.walkable = transparent; // прозрачные = проходимые
                         }
                         world_.set_overlay(x, y, z, t);
                     });

    world_.set_generator_callback([this](int cx, int cy, int cz)
                                  { this->on_generate_chunk(cx, cy, cz); });

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
        "update_render",
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

                auto js_depth = p.attributes().get("render_depth");
                if (js_depth.is_number_integer()) render_depth = js_depth.get<int>();
                auto js_ceil = p.attributes().get("ceiling_height");
                if (js_ceil.is_number_integer()) ceiling_height = js_ceil.get<int>();

                std::string render_mode = "topdown";
                auto js_mode = p.attributes().get("render_mode");
                if (js_mode.is_string()) render_mode = js_mode.get<std::string>();

                std::string axis = "xy";
                auto js_axis = p.attributes().get("view_axis");
                if (js_axis.is_string()) axis = js_axis.get<std::string>();

                std::string dir = "t";
                auto js_dir = p.attributes().get("view_dir");
                if (js_dir.is_string()) dir = js_dir.get<std::string>();

                int w = radius * 2 + 1;
                int h = radius * 2 + 1;
                nlohmann::json grid = nlohmann::json::array();

                for (int v = -radius; v <= radius; ++v)
                {
                    for (int u = -radius; u <= radius; ++u)
                    {
                        nlohmann::json best_cell = nullptr;

                        if (axis == "xy")
                        {
                            int gx = px + u;
                            int gy = py + v;

                            int start_z, end_z, step_z;
                            if (render_mode == "level")
                            {
                                start_z = pz;
                                end_z = pz;
                                step_z = 1;
                            }
                            else if (dir == "t")
                            {
                                start_z = pz + ceiling_height;
                                end_z = pz - render_depth;
                                step_z = -1;
                            }
                            else
                            {
                                start_z = pz - render_depth;
                                end_z = pz + ceiling_height;
                                step_z = 1;
                            }

                            std::string terrain_bg = "000000";
                            std::string best_sym = "";
                            std::string best_fg = "";
                            int best_z = pz;
                            bool found_terrain = false;
                            bool found_something = false;

                            for (int z = start_z; (step_z < 0 ? z >= end_z : z <= end_z);
                                 z += step_z)
                            {
                                // ИГРОК - ПРОВЕРЯЕМ ПЕРВЫМ ДЕЛОМ
                                if (gx == px && gy == py && z == pz)
                                {
                                    // ИСПРАВЛЕНО: ищем пол под игроком (может быть глубже)
                                    int floor_z = pz - 1;
                                    std::string floor_color = "000000";
                                    while (floor_z > pz - 20)
                                    {
                                        bool f = false;
                                        if (world_.has_overlay(gx, gy, floor_z))
                                        {
                                            Tile ov = world_.get_overlay(gx, gy, floor_z);
                                            if (!ov.symbol.empty())
                                            {
                                                floor_color = ov.color_hex;
                                                f = true;
                                            }
                                        }
                                        if (!f)
                                        {
                                            Tile tr = world_.get_terrain(gx, gy, floor_z);
                                            if (!tr.symbol.empty())
                                            {
                                                floor_color = tr.color_hex;
                                                f = true;
                                            }
                                        }
                                        if (f) break;
                                        floor_z--;
                                    }
                                    terrain_bg = floor_color;
                                    best_sym = "@";
                                    best_fg = "FFD700";
                                    best_z = floor_z; // знаковое d = floor_z - pz (negative)
                                    found_something = true;
                                    break;
                                }

                                bool has_ov = world_.has_overlay(gx, gy, z);
                                Tile ov = has_ov ? world_.get_overlay(gx, gy, z) : Tile{};
                                Tile tr = world_.get_terrain(gx, gy, z);

                                if (has_ov && !ov.symbol.empty())
                                {
                                    if (!found_something)
                                    {
                                        best_sym = ov.symbol;
                                        best_fg = ov.color_hex.empty() ? "FFFFFF" : ov.color_hex;
                                        best_z = z;
                                        found_something = true;
                                    }
                                    if (!ov.transparent) break;
                                }

                                if (!tr.symbol.empty())
                                {
                                    if (!found_terrain)
                                    {
                                        terrain_bg = tr.color_hex.empty() ? "000000" : tr.color_hex;
                                        found_terrain = true;
                                        if (!found_something)
                                        {
                                            best_sym = " ";
                                            best_fg = terrain_bg;
                                            best_z = z;
                                            found_something = true;
                                        }
                                    }
                                    if (!tr.transparent) break;
                                }

                                if (render_mode == "level" && z == pz) break;
                            }

                            if (found_something)
                            {
                                // ИСПРАВЛЕНО: d_val знаковое (положительное = выше, отрицательное =
                                // ниже)
                                int d_val = best_z - pz;
                                int r_val =
                                    static_cast<int>(std::sqrt(static_cast<double>(u * u + v * v)));
                                best_cell = {{"s", best_sym}, {"f", best_fg}, {"b", terrain_bg},
                                             {"d", d_val},    {"r", r_val},   {"z", best_z}};
                            }
                        }
                        else if (axis == "xz" || axis == "yz")
                        {
                            int start_x = px, start_y = py, start_z = pz;
                            int dx = 0, dy = 0;

                            if (axis == "xz")
                            {
                                start_x = px + u;
                                start_z = pz - v;
                                // ИСПРАВЛЕНО: старт на уровне игрока, луч идёт вглубь
                                if (dir == "t")
                                {
                                    start_y = py;
                                    dy = 1;
                                }
                                else
                                {
                                    start_y = py;
                                    dy = -1;
                                }
                            }
                            else
                            {
                                start_y = py + u;
                                start_z = pz - v;
                                if (dir == "t")
                                {
                                    start_x = px;
                                    dx = 1;
                                }
                                else
                                {
                                    start_x = px;
                                    dx = -1;
                                }
                            }

                            int cx = start_x, cy = start_y, cz = start_z;
                            int dist = 0;

                            std::string terrain_bg = "000000";
                            std::string best_sym = "";
                            std::string best_fg = "";
                            int best_z = pz;
                            bool found_something = false;

                            while (dist < 50)
                            {
                                // ИГРОК - ПЕРВЫМ ДЕЛОМ
                                if (cx == px && cy == py && cz == pz)
                                {
                                    Tile tr = world_.get_terrain(cx, cy, cz);
                                    terrain_bg = tr.symbol.empty() ? "000000" : tr.color_hex;
                                    best_sym = "@";
                                    best_fg = "FFD700";
                                    best_z = pz;
                                    found_something = true;
                                    break;
                                }

                                bool has_ov = world_.has_overlay(cx, cy, cz);
                                Tile ov = has_ov ? world_.get_overlay(cx, cy, cz) : Tile{};
                                Tile tr = world_.get_terrain(cx, cy, cz);

                                if (has_ov && !ov.symbol.empty())
                                {
                                    if (!found_something)
                                    {
                                        best_sym = ov.symbol;
                                        best_fg = ov.color_hex.empty() ? "FFFFFF" : ov.color_hex;
                                        terrain_bg = tr.symbol.empty() ? "000000" : tr.color_hex;
                                        best_z = cz;
                                        found_something = true;
                                    }
                                    if (!ov.transparent) break;
                                }

                                if (!tr.symbol.empty())
                                {
                                    if (!found_something)
                                    {
                                        best_sym = " ";
                                        best_fg = tr.color_hex.empty() ? "808080" : tr.color_hex;
                                        terrain_bg = best_fg;
                                        best_z = cz;
                                        found_something = true;
                                    }
                                    if (!tr.transparent) break;
                                }

                                cx += dx;
                                cy += dy; // cz не меняется (луч горизонтальный)
                                dist++;
                            }

                            if (found_something)
                            {
                                int d_val = best_z - pz;
                                best_cell = {{"s", best_sym}, {"f", best_fg}, {"b", terrain_bg},
                                             {"d", d_val},    {"r", dist},    {"z", best_z}};
                            }
                        }

                        grid.push_back(best_cell);
                    }
                }

                if (map_update_cb_)
                {
                    nlohmann::json payload;
                    payload["c"] = {px, py, pz};
                    payload["a"] = axis;
                    payload["d"] = dir;
                    payload["m"] = render_mode;
                    payload["w"] = w;
                    payload["h"] = h;
                    payload["g"] = grid;
                    map_update_cb_(p.id(), payload);
                }
            }
            catch (const std::exception& e)
            {
                MCG_ERROR("Exception in update_render: {}", e.what());
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