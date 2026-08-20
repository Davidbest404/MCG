#include "ServerLua.h"
#include "ServerShared.h"          // <-- теперь видит все типы
#include "../Common/ConsoleHelper.h"
#include <iostream>
#include <mutex>
#include <unordered_set>
#include <windows.h>
#include <string>
#include <variant>
#include <map>

using namespace std;

// ---------- Реализации Lua-функций ----------
static int lua_get_hp(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    lua_pushinteger(L, (it != game_state.players.end()) ? it->second.hp : 0);
    return 1;
}

static int lua_set_hp(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    int hp = luaL_checkinteger(L, 2);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    if (it != game_state.players.end()) {
        it->second.hp = max(0, min(hp, it->second.max_hp));
    }
    return 0;
}

static int lua_get_max_hp(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    lua_pushinteger(L, (it != game_state.players.end()) ? it->second.max_hp : 0);
    return 1;
}

static int lua_set_max_hp(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    int max_hp = luaL_checkinteger(L, 2);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    if (it != game_state.players.end()) {
        it->second.max_hp = max_hp;
        if (it->second.hp > max_hp) it->second.hp = max_hp;
    }
    return 0;
}

static int lua_get_x(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    lua_pushinteger(L, (it != game_state.players.end()) ? it->second.x : 0);
    return 1;
}

static int lua_set_x(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    int x = luaL_checkinteger(L, 2);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    if (it != game_state.players.end()) it->second.x = x;
    return 0;
}

static int lua_get_y(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    lua_pushinteger(L, (it != game_state.players.end()) ? it->second.y : 0);
    return 1;
}

static int lua_set_y(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    if (it != game_state.players.end()) it->second.y = y;
    return 0;
}

static int lua_set_can_move(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    bool can = lua_toboolean(L, 2) != 0;
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    if (it != game_state.players.end()) {
        it->second.can_move = can;
    }
    return 0;
}

static int lua_get_can_move(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    bool can = (it != game_state.players.end()) ? it->second.can_move : true;
    lua_pushboolean(L, can);
    return 1;
}

static int lua_get_player_name(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    lua_pushstring(L, (it != game_state.players.end()) ? it->second.name.c_str() : "");
    return 1;
}

static int lua_send_to_player(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    const char* msg = luaL_checkstring(L, 2);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    if (it != game_state.players.end() && it->second.sock != INVALID_SOCKET) {
        string full = string(msg) + "\n";
        send(it->second.sock, full.c_str(), (int)full.size(), 0);
    }
    return 0;
}

static int lua_broadcast(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    broadcast_message(msg, INVALID_SOCKET);
    return 0;
}

static int lua_get_all_players(lua_State* L) {
    lock_guard<mutex> lock(game_mutex);
    lua_newtable(L);
    int index = 1;
    for (const auto& pair : game_state.players) {
        lua_pushinteger(L, index++);
        lua_pushinteger(L, pair.first);
        lua_settable(L, -3);
    }
    return 1;
}

static int lua_get_players_in_radius(lua_State* L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int radius = luaL_checkinteger(L, 3);
    lock_guard<mutex> lock(game_mutex);
    lua_newtable(L);
    int index = 1;
    for (const auto& pair : game_state.players) {
        const Player& p = pair.second;
        int dx = p.x - x;
        int dy = p.y - y;
        int dist = abs(dx) + abs(dy);
        if (dist <= radius) {
            lua_pushinteger(L, index++);
            lua_pushinteger(L, pair.first);
            lua_settable(L, -3);
        }
    }
    return 1;
}

static int lua_get_distance(lua_State* L) {
    int x1 = luaL_checkinteger(L, 1);
    int y1 = luaL_checkinteger(L, 2);
    int x2 = luaL_checkinteger(L, 3);
    int y2 = luaL_checkinteger(L, 4);
    int dist = abs(x1 - x2) + abs(y1 - y2);
    lua_pushinteger(L, dist);
    return 1;
}

static int lua_get_attr(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    const char* key = luaL_checkstring(L, 2);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    if (it != game_state.players.end()) {
        const auto& attrs = it->second.attrs;
        auto ait = attrs.find(key);
        if (ait != attrs.end()) {
            if (holds_alternative<int>(ait->second))
                lua_pushinteger(L, get<int>(ait->second));
            else if (holds_alternative<float>(ait->second))
                lua_pushnumber(L, get<float>(ait->second));
            else if (holds_alternative<string>(ait->second))
                lua_pushstring(L, get<string>(ait->second).c_str());
            else if (holds_alternative<bool>(ait->second))
                lua_pushboolean(L, get<bool>(ait->second));
            else
                lua_pushnil(L);
        }
        else {
            lua_pushnil(L);
        }
    }
    else {
        lua_pushnil(L);
    }
    return 1;
}

static int lua_set_attr(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    const char* key = luaL_checkstring(L, 2);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    if (it != game_state.players.end()) {
        if (lua_isinteger(L, 3))
            it->second.setAttr(key, (int)lua_tointeger(L, 3));
        else if (lua_isnumber(L, 3))
            it->second.setAttr(key, (float)lua_tonumber(L, 3));
        else if (lua_isstring(L, 3))
            it->second.setAttr(key, string(lua_tostring(L, 3)));
        else if (lua_isboolean(L, 3))
            it->second.setAttr(key, (bool)lua_toboolean(L, 3));
        else
            return luaL_error(L, "Unsupported attribute type");
    }
    return 0;
}

// ---------- Регистрация функций ----------
void register_lua_functions(lua_State* L) {
    lua_register(L, "get_hp", lua_get_hp);
    lua_register(L, "set_hp", lua_set_hp);
    lua_register(L, "get_max_hp", lua_get_max_hp);
    lua_register(L, "set_max_hp", lua_set_max_hp);
    lua_register(L, "get_x", lua_get_x);
    lua_register(L, "set_x", lua_set_x);
    lua_register(L, "get_y", lua_get_y);
    lua_register(L, "set_y", lua_set_y);
    lua_register(L, "set_can_move", lua_set_can_move);
    lua_register(L, "get_can_move", lua_get_can_move);
    lua_register(L, "get_player_name", lua_get_player_name);
    lua_register(L, "send_to_player", lua_send_to_player);
    lua_register(L, "broadcast", lua_broadcast);
    lua_register(L, "get_all_players", lua_get_all_players);
    lua_register(L, "get_players_in_radius", lua_get_players_in_radius);
    lua_register(L, "get_distance", lua_get_distance);
    lua_register(L, "get_attr", lua_get_attr);
    lua_register(L, "set_attr", lua_set_attr);
}

// ---------- Загрузка Lua-скриптов ----------
void LoadLuaScripts(lua_State* L) {
    {
        lock_guard<mutex> lock(lua_desc_mutex);
        lua_command_descriptions.clear();
    }

    CreateDirectoryA("actions", NULL);
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA("actions/*.lua", &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        cout << "No Lua scripts in actions/ folder." << endl;
        return;
    }
    unordered_set<string> new_available;
    do {
        string filename = "actions/" + string(findData.cFileName);
        if (luaL_dofile(L, filename.c_str()) != LUA_OK) {
            cerr << "Lua error in " << filename << ": " << lua_tostring(L, -1) << endl;
            lua_pop(L, 1);
            continue;
        }

        string cmd_name = findData.cFileName;
        size_t dot = cmd_name.find_last_of('.');
        if (dot != string::npos) cmd_name = cmd_name.substr(0, dot);
        new_available.insert(cmd_name);

        lua_getglobal(L, "get_description");
        if (lua_isfunction(L, -1)) {
            if (lua_pcall(L, 0, 1, 0) == LUA_OK) {
                const char* desc = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
                lock_guard<mutex> lock(lua_desc_mutex);
                lua_command_descriptions[cmd_name] = desc;
                lua_pop(L, 1);
            }
            else {
                cerr << "Error calling get_description() in " << filename << endl;
                lua_pop(L, 1);
                lock_guard<mutex> lock(lua_desc_mutex);
                lua_command_descriptions[cmd_name] = "";
            }
        }
        else {
            lua_pop(L, 1);
            lock_guard<mutex> lock(lua_desc_mutex);
            lua_command_descriptions[cmd_name] = "";
        }

        cout << "Loaded: " << filename << endl;
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    {
        lock_guard<mutex> lock(lua_commands_mutex);
        available_lua_commands = move(new_available);
        for (auto it = active_lua_commands.begin(); it != active_lua_commands.end(); ) {
            if (available_lua_commands.find(*it) == available_lua_commands.end())
                it = active_lua_commands.erase(it);
            else
                ++it;
        }
    }
}