// lua_engine.h
#pragma once

#include <memory>
#include <sol/sol.hpp>
#include <string>

namespace mcg::server
{

class Player;

class LuaEngine
{
public:
    LuaEngine();
    ~LuaEngine();

    // Выполнить файл скрипта
    void run_script(const std::string& path);

    // Вызвать функцию on_player_connect, если она есть
    void on_player_connect(std::shared_ptr<Player> player);

private:
    sol::state lua_;
};

} // namespace mcg::server