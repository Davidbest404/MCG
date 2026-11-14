#include "server/lua/lua_engine.h"
#include "server/player/player.h"
#include "shared/util/logging.h"
#include <nlohmann/json.hpp>

namespace mcg::server
{

// Вспомогательная функция: конвертация nlohmann::json в sol::object
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
    lua_.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table);

    // ИСПРАВЛЕНО: Регистрируем класс Player для Lua
    // Это позволяет вызывать player:id() из Lua
    lua_.new_usertype<Player>("Player", "id", &Player::id);

    auto api = lua_["api"].get_or_create<sol::table>();

    // api.get_attr(player, key) -> value
    api.set_function("get_attr",
                     [](sol::this_state ts, Player& p, const std::string& key) -> sol::object
                     {
                         sol::state_view lua(ts);
                         return json_to_sol(lua, p.attributes().get(key));
                     });

    // api.set_attr(player, key, value)
    api.set_function("set_attr",
                     [](Player& p, const std::string& key, sol::object value)
                     {
                         if (value.is<int64_t>())
                         {
                             p.attributes().set(key, value.as<int64_t>());
                         }
                         else if (value.is<double>())
                         {
                             p.attributes().set(key, value.as<double>());
                         }
                         else if (value.is<bool>())
                         {
                             p.attributes().set(key, value.as<bool>());
                         }
                         else if (value.is<std::string>())
                         {
                             p.attributes().set(key, value.as<std::string>());
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