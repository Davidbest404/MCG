#pragma once

#include <map>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <mutex>
#include <atomic>
#include <ctime>
#include <winsock2.h>
#include <lua.hpp>

using namespace std;

// ---------- Типы, общие для сервера и Lua-модуля ----------
enum class ActionType { MOVE, LUA, WAIT, SKIP };

class Player {
public:
    SOCKET sock = INVALID_SOCKET;
    string name;
    int id;
    bool is_admin;
    int hp = 100;
    int max_hp = 100;
    bool is_ready = false;
    ActionType last_action = ActionType::WAIT;
    bool can_move = true;
    int x = 0, y = 0;
    int start_x, start_y;
    int view_radius = 5;
    unordered_map<string, variant<int, float, string, bool>> attrs;

    template<typename T>
    T getAttr(const string& key, const T& defaultValue = {}) const {
        auto it = attrs.find(key);
        if (it != attrs.end() && holds_alternative<T>(it->second))
            return get<T>(it->second);
        return defaultValue;
    }
    void setAttr(const string& key, const variant<int, float, string, bool>& value) { attrs[key] = value; }
    bool hasAttr(const string& key) const { return attrs.count(key); }
    void removeAttr(const string& key) { attrs.erase(key); }
};

struct GameState {
    bool is_active = false;
    time_t turn_start_time;
    int turn_duration_seconds = 1888;
    int current_turn = 1;
    vector<string> turn_log;
    map<int, Player> players;
};

struct Tile {
    int id = 0;
    bool walkable = true;
    string on_enter, on_exit, on_step;
    string display;
};

// ---------- Новая карта: разреженная хеш-таблица ----------
inline long long make_tile_key(int x, int y) {
    return (static_cast<long long>(x) << 32) | (static_cast<unsigned int>(y));
}

// Глобальные переменные для карты (определяются в ServerMap.cpp)
extern unordered_map<long long, int> world_tiles;
extern map<int, Tile> tile_types;

// ---------- Остальные extern-переменные ----------
extern GameState game_state;
extern mutex game_mutex;
extern map<string, string> lua_command_descriptions;
extern mutex lua_desc_mutex;
extern unordered_set<string> available_lua_commands;
extern unordered_set<string> active_lua_commands;
extern mutex lua_commands_mutex;

extern vector<SOCKET> clients;
extern atomic<int> client_count;
extern map<SOCKET, pair<string, int>> client_info;
extern map<SOCKET, bool> admin_clients;
extern atomic<int> next_client_id;
extern string Password, Name;
extern int max_clients;
extern unordered_map<string, variant<int, float, string, bool>> default_attrs;
extern mutex default_attrs_mutex;
extern string server_description, server_rules;
extern mutex server_info_mutex;
extern lua_State* gLuaState;
extern int PORT;

// Прототипы функций, используемых в Lua
void broadcast_message(const string& message, SOCKET sender);