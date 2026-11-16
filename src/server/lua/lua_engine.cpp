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

    api.set_function("get_terrain_color", [this](int x, int y, int z) -> std::string
                     { return world_.get_terrain(x, y, z).color_hex; });

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
                             t.walkable = transparent;
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

                std::string p_sym = "@";
                auto js_sym = p.attributes().get("symbol");
                if (js_sym.is_string()) p_sym = js_sym.get<std::string>();

                std::string p_col = "FFD700";
                auto js_col = p.attributes().get("color");
                if (js_col.is_string()) p_col = js_col.get<std::string>();

                // Способности рас
                bool xray = false;
                auto js_xray = p.attributes().get("xray");
                if (js_xray.is_boolean()) xray = js_xray.get<bool>();

                int w = radius * 2 + 1;
                int h = radius * 2 + 1;
                nlohmann::json grid = nlohmann::json::array();

                for (int v = -radius; v <= radius; ++v)
                {
                    for (int u = -radius; u <= radius; ++u)
                    {
                        nlohmann::json best_cell = nullptr;
                        bool is_player_cell = (u == 0 && v == 0);

                        if (axis == "xy")
                        {
                            int gx = px + u;
                            int gy = py + v;

                            int start_z =
                                (render_mode == "level")
                                    ? pz
                                    : (dir == "t" ? pz + ceiling_height : pz - render_depth);
                            int end_z =
                                (render_mode == "level")
                                    ? pz
                                    : (dir == "t" ? pz - render_depth : pz + ceiling_height);
                            int step_z = (dir == "t" && render_mode != "level") ? -1 : 1;
                            if (render_mode == "level")
                            {
                                start_z = pz;
                                end_z = pz;
                                step_z = 1;
                            }

                            std::string found_sym = "";
                            std::string found_fg = "";
                            std::string found_bg = "000000";
                            int found_z = -9999;
                            int found_bg_z = pz;
                            bool found_player = false;

                            for (int z = start_z; (step_z < 0 ? z >= end_z : z <= end_z);
                                 z += step_z)
                            {
                                if (is_player_cell && z == pz)
                                {
                                    found_player = true;
                                    found_z = z;
                                    if (render_mode == "level") break;
                                    continue;
                                }

                                if (entity_cb_)
                                {
                                    auto [esym, ecol] = entity_cb_(gx, gy, z);
                                    if (!esym.empty())
                                    {
                                        if (found_sym.empty() && !found_player)
                                        {
                                            found_sym = esym;
                                            found_fg = ecol;
                                            found_z = z;
                                        }
                                        break;
                                    }
                                }

                                bool has_ov = world_.has_overlay(gx, gy, z);
                                Tile ov = has_ov ? world_.get_overlay(gx, gy, z) : Tile{};
                                Tile tr = world_.get_terrain(gx, gy, z);

                                if (has_ov && !ov.symbol.empty())
                                {
                                    if (found_sym.empty() && !found_player)
                                    {
                                        found_sym = ov.symbol;
                                        found_fg = ov.color_hex;
                                        found_z = z;
                                    }
                                    if (!ov.transparent && !xray) break;
                                }

                                if (!tr.symbol.empty())
                                {
                                    if (found_bg == "000000")
                                    {
                                        found_bg = tr.color_hex;
                                        found_bg_z = z;
                                    }
                                    if (found_sym.empty() && !found_player)
                                    {
                                        found_sym = " ";
                                        found_fg = tr.color_hex;
                                        found_z = z;
                                    }
                                    if (!tr.transparent && !xray) break;
                                }

                                if (render_mode == "level" && z == pz) break;
                            }

                            if (found_player)
                            {
                                // ФОН ИГРОКА: смотрим прямо под ноги
                                std::string floor_color = "000000";
                                int floor_z = pz - 1;
                                while (floor_z > pz - 20)
                                {
                                    if (world_.has_overlay(px, py, floor_z))
                                    {
                                        Tile ov = world_.get_overlay(px, py, floor_z);
                                        if (!ov.symbol.empty())
                                        {
                                            floor_color = ov.color_hex;
                                            break;
                                        }
                                    }
                                    Tile tr = world_.get_terrain(px, py, floor_z);
                                    if (!tr.symbol.empty())
                                    {
                                        floor_color = tr.color_hex;
                                        break;
                                    }
                                    floor_z--;
                                }
                                int d_bg = floor_z - pz;
                                best_cell = {{"s", p_sym},       {"f", p_col}, {"b", floor_color},
                                             {"d", d_bg},        {"r", 0},     {"z", pz},
                                             {"is_player", true}};
                            }
                            else if (!found_sym.empty())
                            {
                                best_cell = {{"s", found_sym},
                                             {"f", found_fg},
                                             {"b", found_bg},
                                             {"d", found_z - pz},
                                             {"r", (int)std::sqrt(u * u + v * v)},
                                             {"z", found_z}};
                            }
                        }
                        else if (axis == "xz" || axis == "yz")
                        {
                            // SIDE VIEW: смотрим сверху вниз по каждой колонке (x, z) или (y, z)
                            int gx = px, gy = py, gz = pz - v;
                            if (axis == "xz")
                                gx = px + u;
                            else
                                gy = py + u;

                            // Ищем поверхность (highest non-air block) в этой колонке
                            std::string best_sym = "";
                            std::string best_fg = "";
                            std::string best_bg = "000000";
                            int best_other = -9999; // y для xz, x для yz

                            if (axis == "xz")
                            {
                                for (int y = py + 20; y >= py - 20; --y)
                                {
                                    // Сначала проверяем оверлеи (объекты: деревья, цветы, кактусы)
                                    if (world_.has_overlay(gx, y, gz))
                                    {
                                        Tile ov = world_.get_overlay(gx, y, gz);
                                        if (!ov.symbol.empty() && ov.symbol != " ")
                                        {
                                            if (y > best_other)
                                            {
                                                best_sym = ov.symbol;
                                                best_fg = ov.color_hex;
                                                Tile tr = world_.get_terrain(gx, y, gz);
                                                best_bg =
                                                    tr.symbol.empty() ? "000000" : tr.color_hex;
                                                best_other = y;
                                            }
                                            continue;
                                        }
                                    }
                                    Tile tr = world_.get_terrain(gx, y, gz);
                                    if (!tr.symbol.empty())
                                    {
                                        if (y > best_other)
                                        {
                                            best_sym = " ";
                                            best_fg = tr.color_hex;
                                            best_bg = tr.color_hex;
                                            best_other = y;
                                        }
                                    }
                                }
                            }
                            else
                            {
                                for (int x = px + 20; x >= px - 20; --x)
                                {
                                    if (world_.has_overlay(x, gy, gz))
                                    {
                                        Tile ov = world_.get_overlay(x, gy, gz);
                                        if (!ov.symbol.empty() && ov.symbol != " ")
                                        {
                                            if (x > best_other)
                                            {
                                                best_sym = ov.symbol;
                                                best_fg = ov.color_hex;
                                                Tile tr = world_.get_terrain(x, gy, gz);
                                                best_bg =
                                                    tr.symbol.empty() ? "000000" : tr.color_hex;
                                                best_other = x;
                                            }
                                            continue;
                                        }
                                    }
                                    Tile tr = world_.get_terrain(x, gy, gz);
                                    if (!tr.symbol.empty())
                                    {
                                        if (x > best_other)
                                        {
                                            best_sym = " ";
                                            best_fg = tr.color_hex;
                                            best_bg = tr.color_hex;
                                            best_other = x;
                                        }
                                    }
                                }
                            }

                            if (is_player_cell)
                            {
                                std::string floor_color = "000000";
                                int floor_z = pz - 1;
                                while (floor_z > pz - 20)
                                {
                                    if (world_.has_overlay(px, py, floor_z))
                                    {
                                        Tile ov = world_.get_overlay(px, py, floor_z);
                                        if (!ov.symbol.empty())
                                        {
                                            floor_color = ov.color_hex;
                                            break;
                                        }
                                    }
                                    Tile tr = world_.get_terrain(px, py, floor_z);
                                    if (!tr.symbol.empty())
                                    {
                                        floor_color = tr.color_hex;
                                        break;
                                    }
                                    floor_z--;
                                }
                                int d_bg = floor_z - pz;
                                best_cell = {{"s", p_sym},       {"f", p_col}, {"b", floor_color},
                                             {"d", d_bg},        {"r", 0},     {"z", pz},
                                             {"is_player", true}};
                            }
                            else if (best_other != -9999)
                            {
                                int d_z = gz - pz;
                                best_cell = {{"s", best_sym}, {"f", best_fg}, {"b", best_bg},
                                             {"d", d_z},      {"r", 0},       {"z", gz}};
                            }
                        }
                        grid.push_back(best_cell);
                    }
                }

                if (map_update_cb_)
                {
                    float sun_strength = 0.5f;
                    auto js_ss = p.attributes().get("sun_strength");
                    if (js_ss.is_number()) sun_strength = js_ss.get<float>();

                    bool monochrome = false;
                    auto js_mono = p.attributes().get("monochrome");
                    if (js_mono.is_boolean()) monochrome = js_mono.get<bool>();

                    bool no_fog = false;
                    auto js_nofog = p.attributes().get("no_fog");
                    if (js_nofog.is_boolean()) no_fog = js_nofog.get<bool>();

                    std::string sun_col = "FFF8DC", dark_col = "000000", sky_low = "200040",
                                sky_mid = "87CEEB", sky_high = "FFFFFF";

                    auto js_sun = p.attributes().get("sun_color");
                    if (js_sun.is_string()) sun_col = js_sun.get<std::string>();
                    auto js_dark = p.attributes().get("dark_color");
                    if (js_dark.is_string()) dark_col = js_dark.get<std::string>();
                    auto js_sl = p.attributes().get("sky_low_color");
                    if (js_sl.is_string()) sky_low = js_sl.get<std::string>();
                    auto js_sm = p.attributes().get("sky_mid_color");
                    if (js_sm.is_string()) sky_mid = js_sm.get<std::string>();
                    auto js_sh = p.attributes().get("sky_high_color");
                    if (js_sh.is_string()) sky_high = js_sh.get<std::string>();

                    nlohmann::json payload;
                    payload["c"] = {px, py, pz};
                    payload["a"] = axis;
                    payload["d"] = dir;
                    payload["m"] = render_mode;
                    payload["w"] = w;
                    payload["h"] = h;
                    payload["g"] = grid;
                    payload["sun"] = sun_col;
                    payload["dark"] = dark_col;
                    payload["sky_low"] = sky_low;
                    payload["sky_mid"] = sky_mid;
                    payload["sky_high"] = sky_high;
                    payload["sun_strength"] = sun_strength;
                    payload["monochrome"] = monochrome;
                    payload["no_fog"] = no_fog;
                    map_update_cb_(p.id(), payload);
                }
            }
            catch (const std::exception& e)
            {
                MCG_ERROR("Exception in update_render: {}", e.what());
            }
        });

    api.set_function("set_race",
                     [this](Player& p, const std::string& race_name)
                     {
                         auto it = races_.find(race_name);
                         if (it == races_.end()) return false;
                         for (auto& [k, v] : it->second.items())
                         {
                             if (v.is_string())
                                 p.attributes().set(k, v.get<std::string>());
                             else if (v.is_number_integer())
                                 p.attributes().set(k, v.get<int64_t>());
                             else if (v.is_number_float())
                                 p.attributes().set(k, v.get<double>());
                             else if (v.is_boolean())
                                 p.attributes().set(k, v.get<bool>());
                         }
                         p.attributes().set("race", race_name);
                         return true;
                     });

    api.set_function("get_race",
                     [](Player& p) -> std::string
                     {
                         auto js = p.attributes().get("race");
                         return js.is_string() ? js.get<std::string>() : "base";
                     });

    api.set_function("list_races",
                     [this]() -> std::vector<std::string>
                     {
                         std::vector<std::string> out;
                         for (auto& [k, _] : races_)
                             out.push_back(k);
                         return out;
                     });
}

LuaEngine::~LuaEngine() = default;

void LuaEngine::load_decorators(const std::string& biomes_dir)
{
    namespace fs = std::filesystem;
    if (!fs::exists(biomes_dir))
    {
        MCG_WARN("Biomes dir not found: {}", biomes_dir);
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

void LuaEngine::load_races(const std::string& races_dir)
{
    namespace fs = std::filesystem;
    if (!fs::exists(races_dir))
    {
        MCG_WARN("Races dir not found: {}", races_dir);
        return;
    }
    for (const auto& entry : fs::directory_iterator(races_dir))
    {
        if (entry.path().extension() != ".lua") continue;
        try
        {
            auto result = lua_.script_file(entry.path().string());
            if (!result.valid() || result.get_type() != sol::type::table) continue;
            sol::table t = result;
            std::string name = t["name"].get_or<std::string>("");
            if (name.empty()) continue;

            nlohmann::json attrs = nlohmann::json::object();
            sol::object attrs_obj = t["attributes"];
            if (attrs_obj.valid() && attrs_obj.is<sol::table>())
            {
                for (auto& kv : attrs_obj.as<sol::table>())
                {
                    std::string k = kv.first.as<std::string>();
                    sol::object v = kv.second;
                    if (v.is<std::string>())
                        attrs[k] = v.as<std::string>();
                    else if (v.is<int64_t>())
                        attrs[k] = v.as<int64_t>();
                    else if (v.is<double>())
                        attrs[k] = v.as<double>();
                    else if (v.is<bool>())
                        attrs[k] = v.as<bool>();
                }
            }
            races_[name] = attrs;
            MCG_INFO("Loaded race: {}", name);
        }
        catch (const sol::error& e)
        {
            MCG_ERROR("Failed to load race {}: {}", entry.path().string(), e.what());
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
                cmd.on_command = on_cmd.as<sol::protected_function>();

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