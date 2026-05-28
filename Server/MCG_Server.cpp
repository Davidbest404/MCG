#include "lua.hpp"   // вместо трёх отдельных include
#include <iostream>
#include <string>
#include <random>
#include <fstream>
#include <cstring>
#include <thread>
#include <vector>
#include <map>
#include <algorithm>
#include <atomic>
#include <ctime>
#include <mutex>
#include <sstream>
#include <any>
#include <unordered_map>
#include <functional>
#include <memory>
#include <variant>
// Только Windows
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

lua_State* gLuaState = nullptr;
// Описания Lua-команд
map<string, string> lua_command_descriptions;
mutex lua_desc_mutex;

int PORT = 8080;

// Утилиты консоли
class ConsoleHelper {
public:
    static void InitConsole() {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode = 0;
        GetConsoleMode(hOut, &dwMode);
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
        SetConsoleFont();
    }

    static void SetConsoleFont() {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_FONT_INFOEX fontInfo;
        fontInfo.cbSize = sizeof(fontInfo);
        GetCurrentConsoleFontEx(hConsole, FALSE, &fontInfo);
        wcscpy_s(fontInfo.FaceName, L"Consolas");
        SetCurrentConsoleFontEx(hConsole, FALSE, &fontInfo);
    }

    static void SetColor(int textColor, int bgColor = 0) {
        HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hStdOut, (WORD)((bgColor << 4) | textColor));
    }
};
/*
Black - 0
Blue - 1
Green - 2
Cyan - 3
Red - 4
Purple - 5
Brown - 6
Light gray - 7
Dark gray - 8
Light blue - 9
Light green - 10
Light blue - 11 - A
Light red - 12 - B
Light purple - 13 - C
Yellow - 14 - E
White - 15 - F
Преобразует hex-символ (0-9, A-F, a-f) в число 0-15
*/

enum class ActionType {
    MOVE,
    LUA,
    WAIT,
    SKIP
};

class Player {
public:
    //обязательные системные поля
    SOCKET sock = INVALID_SOCKET;
    string name;
    int id;
    bool is_admin;
    int hp = 100;
    int max_hp = 100;
    bool is_ready = false;
    ActionType last_action = ActionType::WAIT;
    bool can_move = true;
    int x = 0;
    int y = 0;

    // Динамические атрибуты
    unordered_map<string, variant<int, float, string, bool>> attrs;

    template<typename T>
    T getAttr(const string& key, const T& defaultValue = {}) const {
        auto it = attrs.find(key);
        if (it != attrs.end() && holds_alternative<T>(it->second))
            return get<T>(it->second);
        return defaultValue;
    }

    void setAttr(const string& key, const variant<int, float, string, bool>& value) {
        attrs[key] = value;
    }

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

vector<SOCKET> clients;
atomic<int> client_count(0);
map<SOCKET, pair<string, int>> client_info;
map<SOCKET, bool> admin_clients;
atomic<int> next_client_id(1);
string Password = "null";
string Name = "Player";
int max_clients = 10;
GameState game_state;
mutex game_mutex;
// Атрибуты по умолчанию для новых игроков
unordered_map<string, variant<int, float, string, bool>> default_attrs;
mutex default_attrs_mutex;  // для потокобезопасности (опционально, но для порядка)
// Описание и правила сервера (редактируемые)
string server_description = "MCG - multiconsole game!";
string server_rules = "1. Respect other players.\n2. No cheating.\n3. Have fun!";
mutex server_info_mutex;

void apply_default_attrs(Player& player) {
    lock_guard<mutex> lock(default_attrs_mutex);
    for (const auto& [key, val] : default_attrs) {
        player.setAttr(key, val);
    }
}

// Удаление атрибута у всех игроков
void remove_attr_from_all(const string& attr_name) {
    lock_guard<mutex> lock(game_mutex);
    for (auto& [id, player] : game_state.players) {
        player.removeAttr(attr_name);
    }
}

// Прототипы
void broadcast_message(const string& message, SOCKET sender);
void handle_client(SOCKET client_sock);
void game_timer_thread();
void process_turn_end();
void process_game_command(SOCKET client_sock, const string& command, int player_id, bool is_admin);
void send_time_remaining(SOCKET client_sock);
void save_game_state(const string& filename);
void load_game_state(const string& filename);
void auto_save_thread();
// Lua support
void LoadLuaScripts();
void register_lua_functions();
int lua_get_hp(lua_State* L);
int lua_set_hp(lua_State* L);
int lua_get_max_hp(lua_State* L);
int lua_set_max_hp(lua_State* L);
int lua_get_x(lua_State* L);
int lua_set_x(lua_State* L);
int lua_get_y(lua_State* L);
int lua_set_y(lua_State* L);
int lua_send_to_player(lua_State* L);
int lua_broadcast(lua_State* L);
int lua_get_player_name(lua_State* L);

int Random(int max, int min) {
    return rand() % (max - min + 1) + min;
}

bool network_init() {
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        cerr << "WSAStartup failed" << endl;
        return false;
    }
    return true;
}

void network_cleanup() {
    WSACleanup();
}

void broadcast_message(const string& message, SOCKET sender) {
    vector<SOCKET> temp_clients;
    {
        static mutex clients_mutex;
        lock_guard<mutex> lock(clients_mutex);
        temp_clients = clients;
    }
    for (auto client : temp_clients) {
        if (client != sender) {
            send(client, message.c_str(), static_cast<int>(message.length()), 0);
        }
    }
}

void game_timer_thread() {
    while (true) {
        this_thread::sleep_for(chrono::seconds(1));
        bool should_process_turn = false;
        {
            lock_guard<mutex> lock(game_mutex);
            if (game_state.is_active) {
                time_t current_time = time(nullptr);
                time_t elapsed = current_time - game_state.turn_start_time;
                if (elapsed >= game_state.turn_duration_seconds) {
                    should_process_turn = true;
                }
            }
        }
        if (should_process_turn) {
            process_turn_end();
            int minutes = static_cast<int>(game_state.turn_duration_seconds / 60);
            int seconds = static_cast<int>(game_state.turn_duration_seconds - (minutes * 60));
            if (seconds >= 10) {
                broadcast_message("\n=== Turn automatically ended by timer ===\n", INVALID_SOCKET);
            }
        }
    }
}

void process_turn_end() {
    lock_guard<mutex> lock(game_mutex);
    bool all_ready = true;
    for (auto& pair : game_state.players) {
        if (!pair.second.is_ready) {
            all_ready = false;
            break;
        }
    }

    string turn_summary = "\n=== Turn " + to_string(game_state.current_turn) + " Summary ===\n";
    for (auto& pair : game_state.players) {
        auto& p = pair.second;
        switch (p.last_action) {
        case ActionType::MOVE:
            turn_summary += p.name + " moved to position (" + to_string(p.x) + "," + to_string(p.y) + ")\n";
            break;
        case ActionType::LUA:
            turn_summary += p.name + " used dynamic(Lua) command\n";
            break;
        default:
            turn_summary += p.name + " waited.\n";
        }
        p.is_ready = false;
    }
    turn_summary += "=============================\n";
    game_state.turn_log.push_back(turn_summary);
    game_state.current_turn++;
    game_state.turn_start_time = time(nullptr);
}

void send_time_remaining(SOCKET client_sock) {
    if (!game_state.is_active) return;
    time_t current_time = time(nullptr);
    time_t elapsed = current_time - game_state.turn_start_time;
    time_t remaining = game_state.turn_duration_seconds - elapsed;
    if (remaining > 0) {
        int minutes = static_cast<int>(remaining / 60);
        int seconds = static_cast<int>(remaining % 60);
        string time_msg = "Time remaining: " + to_string(minutes) + "m " + to_string(seconds) + "s\n";
        send(client_sock, time_msg.c_str(), static_cast<int>(time_msg.length()), 0);
    }
}

void process_game_command(SOCKET client_sock, const string& command, int player_id, bool is_admin) {
    if (!game_mutex.try_lock()) {
        send(client_sock, "Server is busy processing other commands. Please try again.\n", 68, 0);
        return;
    }
    unique_lock<mutex> lock(game_mutex, adopt_lock);

    if (!game_state.is_active && command != "/start_game" &&
        command.find("/set_") != 0 && command != "/status" && command != "/map") {
        send(client_sock, "Game is not active. Admin must start the game.\n", 52, 0);
        return;
    }

    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd;

    // ----- Динамические Lua-команды -----
    // Освобождаем мьютекс, чтобы Lua не блокировал сервер
    lock.unlock();

    lua_getglobal(gLuaState, cmd.c_str());
    if (lua_isfunction(gLuaState, -1)) {
        lua_pushinteger(gLuaState, player_id);
        // Собираем аргументы в таблицу (args[1], args[2] ...)
        lua_newtable(gLuaState);
        vector<string> args;
        string arg;
        while (iss >> arg) args.push_back(arg);
        for (size_t i = 0; i < args.size(); i++) {
            lua_pushinteger(gLuaState, i + 1);
            lua_pushstring(gLuaState, args[i].c_str());
            lua_settable(gLuaState, -3);
        }
        if (lua_pcall(gLuaState, 2, 1, 0) != LUA_OK) {
            const char* err = lua_tostring(gLuaState, -1);
            send(client_sock, err, strlen(err), 0);
            send(client_sock, "\n", 1, 0);
            lua_pop(gLuaState, 1);
        }
        else {
            if (lua_isstring(gLuaState, -1)) {
                const char* result = lua_tostring(gLuaState, -1);
                string colored = string("[LUA] ") + result;  // добавляем маркер
                send(client_sock, colored.c_str(), colored.size(), 0);
                send(client_sock, "\n", 1, 0);
            }
            lua_pop(gLuaState, 1);
        }
        return;
    }
    else {
        lua_pop(gLuaState, 1); // удаляем nil
    }

    // Возвращаем блокировку, так как дальше идут встроенные команды
    lock.lock();
    // ----- Конец Lua-команд -----

    if (cmd == "time_remaining") {
        send_time_remaining(client_sock);
    }
    else if (cmd == "move") {
        string direction;
        iss >> direction;
        if (game_state.players.find(player_id) == game_state.players.end()) {
            send(client_sock, "Player not found!\n", 19, 0);
            return;
        }
        auto& player = game_state.players[player_id];
        if (player.is_ready) {
            send(client_sock, "You already made your move this turn!\n", 40, 0);
            return;
        }
        if (!player.can_move) {   // используем поле класса
            send(client_sock, "You cannot move right now (can_move is false).\n", 51, 0);
            return;
        }
        if (direction == "north") player.y++;
        else if (direction == "south") player.y--;
        else if (direction == "east") player.x++;
        else if (direction == "west") player.x--;
        else {
            send(client_sock, "Invalid direction. Use: north, south, east, west\n", 50, 0);
            return;
        }
        player.last_action = ActionType::MOVE;
        string response = "You will move to " + direction + ". Position: (" +
            to_string(player.x) + "," + to_string(player.y) + ")\n";
        send(client_sock, response.c_str(), static_cast<int>(response.length()), 0);
        string broadcast_msg = player.name + " will move to " + direction + ".";
        lock.unlock();
        broadcast_message(broadcast_msg, client_sock);
    }
    else if (cmd == "skip") {
        if (game_state.players.find(player_id) == game_state.players.end()) {
            send(client_sock, "Player not found!\n", 19, 0);
            return;
        }
        auto& player = game_state.players[player_id];
        player.last_action = ActionType::SKIP;
        player.is_ready = true;
        send(client_sock, "You skipped your turn.\n", 24, 0);
    }
    else if (cmd == "ready") {
        if (game_state.players.find(player_id) == game_state.players.end()) {
            send(client_sock, "Player not found!\n", 19, 0);
            return;
        }
        auto& player = game_state.players[player_id];
        player.is_ready = true;
        send(client_sock, "You are ready for this turn.\n", 30, 0);

        bool all_ready = true;
        for (auto& pair : game_state.players) {
            if (!pair.second.is_ready) {
                all_ready = false;
                break;
            }
        }
        if (all_ready) {
            lock.unlock();
            broadcast_message("All players are ready! Ending turn...", INVALID_SOCKET);
            process_turn_end();
        }
    }
    else if (cmd == "unready") {
        if (game_state.players.find(player_id) == game_state.players.end()) {
            send(client_sock, "Player not found!\n", 19, 0);
            return;
        }
        auto& player = game_state.players[player_id];
        player.is_ready = false;
        send(client_sock, "You are no longer ready.\n", 26, 0);
    }
    else if (cmd == "status") {
        if (game_state.players.find(player_id) == game_state.players.end()) {
            send(client_sock, "Player not found!\n", 19, 0);
            return;
        }
        auto& player = game_state.players[player_id];
        string status = "[STATUS]\n[cA][bg1]=== Your Status ===[/bg1][/cA]\n";
        status += "Name: " + player.name + "\n";
        status += "HP: " + to_string(player.hp) + "/" + to_string(player.max_hp) + "\n";
        status += "Position: (" + to_string(player.x) + "," + to_string(player.y) + ")\n";
        status += "Can move: " + string(player.can_move ? "Yes" : "No") + "\n";
        status += "Ready: " + string(player.is_ready ? "Yes" : "No") + "\n";
        status += "==================\n";
        send(client_sock, status.c_str(), static_cast<int>(status.length()), 0);
    }
    else if (cmd == "map") {
        string map = "[MAP]\n[c1][bg4] === Game Map === [/bg4][/c1]\n";
        for (int y = 5; y >= -5; y--) {
            for (int x = -5; x <= 5; x++) {
                bool has_player = false;
                for (auto& pair : game_state.players) {
                    if (pair.second.x == x && pair.second.y == y) {
                        map += to_string(pair.second.id);
                        has_player = true;
                        break;
                    }
                }
                if (!has_player) {
                    if (x == 0 && y == 0) map += "X";
                    else map += ".";
                }
                map += " ";
            }
            map += "\n";
        }
        map += "================\n";
        send(client_sock, map.c_str(), static_cast<int>(map.length()), 0);
    }
    else if (is_admin) {
        if (cmd == "start_game") {
            game_state.is_active = true;
            game_state.turn_start_time = time(nullptr);
            game_state.current_turn = 1;
            int minutes = static_cast<int>(game_state.turn_duration_seconds / 60);
            int seconds = static_cast<int>(game_state.turn_duration_seconds - (minutes * 60));
            string broadcast_msg = "=== GAME STARTED ===\nTurn duration: " + to_string(minutes) + "m " + to_string(seconds) + "s\n";
            lock.unlock();
            broadcast_message(broadcast_msg, INVALID_SOCKET);
            ConsoleHelper::SetColor(10);
            cout << "=== GAME STARTED ===\n";
            ConsoleHelper::SetColor(8);
        }
        else if (cmd == "pause_game") {
            game_state.is_active = false;
            lock.unlock();
            broadcast_message("Game paused by admin.", INVALID_SOCKET);
        }
        else if (cmd == "set_turn_time") {
            int seconds;
            iss >> seconds;
            game_state.turn_duration_seconds = seconds;
            game_state.turn_duration_seconds = seconds;
            string broadcast_msg = "Turn duration set to " + to_string(seconds / 60) + " minutes and " + to_string(seconds - ((seconds/60) *60));
            lock.unlock();
            broadcast_message(broadcast_msg, INVALID_SOCKET);
        }
        else if (cmd == "end_turn") {
            lock.unlock();
            process_turn_end();
        }
        else if (cmd == "add_item") {
            send(client_sock, "Item added.\n", 12, 0);
        }
        else if (cmd == "set_hp") {
            int target_id, hp;
            iss >> target_id >> hp;
            if (game_state.players.find(target_id) != game_state.players.end()) {
                game_state.players[target_id].hp = hp;
                send(client_sock, "HP set successfully.\n", 22, 0);
            }
        }
        else if (cmd == "reload_scripts") {
            LoadLuaScripts();
            send(client_sock, "Lua scripts reloaded.\n", 22, 0);
        }
    }
    else {
        send(client_sock, "Unknown command. Type /help for available commands.\n", 55, 0);
    }
}

// ========== ОБРАБОТЧИКИ КОМАНД АТРИБУТОВ ==========

// Установка атрибута у всех текущих игроков
void set_attr_for_all(const string& attr_name, const variant<int, float, string, bool>& value) {
    lock_guard<mutex> lock(game_mutex);
    for (auto& [id, player] : game_state.players) {
        player.setAttr(attr_name, value);
    }
}

// Преобразование строки в variant с автоопределением типа
variant<int, float, string, bool> parse_value(const string& value_str) {
    string low = value_str;
    transform(low.begin(), low.end(), low.begin(), ::tolower);
    if (low == "true") return true;
    if (low == "false") return false;

    char* end = nullptr;
    long long ival = strtoll(value_str.c_str(), &end, 10);
    if (*end == '\0') return (int)ival;

    double dval = strtod(value_str.c_str(), &end);
    if (*end == '\0') return (float)dval;

    return value_str; // строка
}

// Команда /set_attr_all <attr_name> <value>
void handle_set_attr_all(SOCKET client_sock, const string& command) {
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd; // "set_attr_all"
    string attr_name, value_str;
    if (!(iss >> attr_name >> value_str)) {
        string err = "[ERROR]|Usage: /set_attr_all <attr_name> <value>\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    auto var_value = parse_value(value_str);
    set_attr_for_all(attr_name, var_value);
    string ok = "Attribute '" + attr_name + "' set for all current players.\n";
    send(client_sock, ok.c_str(), (int)ok.size(), 0);
}

// /set_attr <target_id> <attr_name> <value>
void handle_set_attr(SOCKET client_sock, const string& command, int caller_id, bool is_admin) {
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd; // "set_attr"
    int target_id;
    string attr_name, value_str;
    if (!(iss >> target_id >> attr_name >> value_str)) {
        string err = "[ERROR]|Usage: /set_attr <target_id> <attr_name> <value>\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(target_id);
    if (it == game_state.players.end()) {
        string err = "[ERROR]|Player not found\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    if (target_id != caller_id && !is_admin) {
        string err = "[ERROR]|You can only modify your own attributes\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    auto var_value = parse_value(value_str);
    it->second.setAttr(attr_name, var_value);
    string ok = "Attribute '" + attr_name + "' set for player " + to_string(target_id) + "\n";
    send(client_sock, ok.c_str(), (int)ok.size(), 0);
}

// /get_attr <target_id> <attr_name>
void handle_get_attr(SOCKET client_sock, const string& command) {
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd; // "get_attr"
    int target_id;
    string attr_name;
    if (!(iss >> target_id >> attr_name)) {
        string err = "[ERROR]|Usage: /get_attr <target_id> <attr_name>\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(target_id);
    if (it == game_state.players.end()) {
        string err = "[ERROR]|Player not found\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    if (!it->second.hasAttr(attr_name)) {
        string err = "[ERROR]|Attribute '" + attr_name + "' not found\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    string result = "Player " + to_string(target_id) + " (" + it->second.name + ") attribute '" + attr_name + "' = ";
    const auto& val = it->second.attrs[attr_name];
    if (holds_alternative<int>(val)) result += to_string(get<int>(val));
    else if (holds_alternative<float>(val)) result += to_string(get<float>(val));
    else if (holds_alternative<string>(val)) result += "\"" + get<string>(val) + "\"";
    else if (holds_alternative<bool>(val)) result += (get<bool>(val) ? "true" : "false");
    result += "\n";
    send(client_sock, result.c_str(), (int)result.size(), 0);
}

// /has_attr <target_id> <attr_name>
void handle_has_attr(SOCKET client_sock, const string& command) {
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd; // "has_attr"
    int target_id;
    string attr_name;
    if (!(iss >> target_id >> attr_name)) {
        string err = "[ERROR]|Usage: /has_attr <target_id> <attr_name>\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(target_id);
    if (it == game_state.players.end()) {
        string err = "[ERROR]|Player not found\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    bool exists = it->second.hasAttr(attr_name);
    string result = "Player " + to_string(target_id) + " (" + it->second.name + ") " +
        (exists ? "has" : "does not have") + " attribute '" + attr_name + "'\n";
    send(client_sock, result.c_str(), (int)result.size(), 0);
}

// /remove_attr <target_id> <attr_name>
void handle_remove_attr(SOCKET client_sock, const string& command, int caller_id, bool is_admin) {
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd; // "remove_attr"
    int target_id;
    string attr_name;
    if (!(iss >> target_id >> attr_name)) {
        string err = "[ERROR]|Usage: /remove_attr <target_id> <attr_name>\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(target_id);
    if (it == game_state.players.end()) {
        string err = "[ERROR]|Player not found\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    if (target_id != caller_id && !is_admin) {
        string err = "[ERROR]|You can only remove your own attributes\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    if (!it->second.hasAttr(attr_name)) {
        string err = "[ERROR]|Attribute '" + attr_name + "' not found\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    it->second.removeAttr(attr_name);
    string ok = "Attribute '" + attr_name + "' removed from player " + to_string(target_id) + "\n";
    send(client_sock, ok.c_str(), (int)ok.size(), 0);
}

// Команда /remove_attr_all <attr_name>
void handle_remove_attr_all(SOCKET client_sock, const string& command) {
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd; // "remove_attr_all"
    string attr_name;
    if (!(iss >> attr_name)) {
        string err = "[ERROR]|Usage: /remove_attr_all <attr_name>\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    remove_attr_from_all(attr_name);
    string ok = "Attribute '" + attr_name + "' removed from all players.\n";
    send(client_sock, ok.c_str(), (int)ok.size(), 0);
}

void handle_default_attr_add(SOCKET client_sock, const string& command) {
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd; // "default_attr_add"
    string attr_name, value_str;
    if (!(iss >> attr_name >> value_str)) {
        string err = "[ERROR]|Usage: /default_attr_add <attr_name> <value>\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    auto var_value = parse_value(value_str);
    {
        lock_guard<mutex> lock(default_attrs_mutex);
        default_attrs[attr_name] = var_value;
    }
    string ok = "Default attribute '" + attr_name + "' set for all future players.\n";
    send(client_sock, ok.c_str(), (int)ok.size(), 0);
}

void handle_default_attr_remove(SOCKET client_sock, const string& command) {
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd; // "default_attr_remove"
    string attr_name;
    if (!(iss >> attr_name)) {
        string err = "[ERROR]|Usage: /default_attr_remove <attr_name>\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    {
        lock_guard<mutex> lock(default_attrs_mutex);
        auto it = default_attrs.find(attr_name);
        if (it == default_attrs.end()) {
            string err = "[ERROR]|Default attribute '" + attr_name + "' not found.\n";
            send(client_sock, err.c_str(), (int)err.size(), 0);
            return;
        }
        default_attrs.erase(it);
    }
    string ok = "Default attribute '" + attr_name + "' removed.\n";
    send(client_sock, ok.c_str(), (int)ok.size(), 0);
}

void handle_default_attr_list(SOCKET client_sock) {
    lock_guard<mutex> lock(default_attrs_mutex);
    if (default_attrs.empty()) {
        string msg = "No default attributes set for future players.\n";
        send(client_sock, msg.c_str(), (int)msg.size(), 0);
        return;
    }
    string msg = "\n=== Default attributes (applied to new players) ===\n";
    for (const auto& [key, val] : default_attrs) {
        msg += key + " : ";
        if (holds_alternative<int>(val)) msg += "int = " + to_string(get<int>(val));
        else if (holds_alternative<float>(val)) msg += "float = " + to_string(get<float>(val));
        else if (holds_alternative<string>(val)) msg += "string = \"" + get<string>(val) + "\"";
        else if (holds_alternative<bool>(val)) msg += "bool = " + string(get<bool>(val) ? "true" : "false");
        msg += "\n";
    }
    msg += "==================================================\n";
    send(client_sock, msg.c_str(), (int)msg.size(), 0);
}

// Синхронизировать атрибуты по умолчанию со всеми существующими игроками
void handle_sync_default_attrs(SOCKET client_sock) {
    // Блокируем оба мьютекса (порядок важен: сначала default_attrs, потом game_state)
    lock_guard<mutex> lock_def(default_attrs_mutex);
    lock_guard<mutex> lock_game(game_mutex);

    if (default_attrs.empty()) {
        string msg = "No default attributes set. Nothing to sync.\n";
        send(client_sock, msg.c_str(), (int)msg.size(), 0);
        return;
    }

    int updated_count = 0;
    for (auto& [id, player] : game_state.players) {
        // Для каждого атрибута по умолчанию – перезаписываем (или добавляем)
        for (const auto& [key, val] : default_attrs) {
            player.setAttr(key, val);
        }
        updated_count++;
    }

    string msg = "Default attributes synchronized to " + to_string(updated_count) + " existing players.\n";
    send(client_sock, msg.c_str(), (int)msg.size(), 0);
}

// /list_attrs [target_id]  — если target_id не указан, то для себя
void handle_list_attrs(SOCKET client_sock, const string& command, int caller_id) {
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd; // "list_attrs"
    int target_id = caller_id; // по умолчанию свой
    if (!(iss >> target_id)) {
        // не указан — оставляем caller_id
    }
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(target_id);
    if (it == game_state.players.end()) {
        string err = "[ERROR]|Player not found\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    Player& p = it->second;
    if (p.attrs.empty()) {
        string msg = "Player " + to_string(target_id) + " (" + p.name + ") has no attributes.\n";
        send(client_sock, msg.c_str(), (int)msg.size(), 0);
        return;
    }
    string msg = "\n=== Attributes of " + p.name + " (ID:" + to_string(target_id) + ") ===\n";
    for (const auto& [key, val] : p.attrs) {
        msg += key + " : ";
        if (holds_alternative<int>(val)) msg += "int = " + to_string(get<int>(val));
        else if (holds_alternative<float>(val)) msg += "float = " + to_string(get<float>(val));
        else if (holds_alternative<string>(val)) msg += "string = \"" + get<string>(val) + "\"";
        else if (holds_alternative<bool>(val)) msg += "bool = " + string(get<bool>(val) ? "true" : "false");
        msg += "\n";
    }
    msg += "===================================\n";
    send(client_sock, msg.c_str(), (int)msg.size(), 0);
}

void save_game_state(const string& filename) {
    ofstream file(filename);
    if (!file.is_open()) {
        cerr << "Failed to save game state to " << filename << endl;
        return;
    }
    lock_guard<mutex> lock(game_mutex);

    // Версия формата (для совместимости)
    file << 2 << endl;

    file << game_state.current_turn << endl;
    file << game_state.turn_duration_seconds << endl;
    file << game_state.is_active << endl;
    file << game_state.players.size() << endl;

    for (auto& pair : game_state.players) {
        auto& player = pair.second;
        // Основные поля
        file << player.name << " "
            << player.hp << " "
            << player.max_hp << " "
            << player.x << " "
            << player.y << " "
            << player.is_admin
            << player.can_move;


        // Сохраняем динамические атрибуты
        file << " " << player.attrs.size();
        for (const auto& attr : player.attrs) {
            file << " " << attr.first;
            if (holds_alternative<int>(attr.second)) {
                file << " int " << get<int>(attr.second);
            }
            else if (holds_alternative<float>(attr.second)) {
                file << " float " << get<float>(attr.second);
            }
            else if (holds_alternative<string>(attr.second)) {
                file << " string " << get<string>(attr.second);
            }
            else if (holds_alternative<bool>(attr.second)) {
                file << " bool " << (get<bool>(attr.second) ? 1 : 0);
            }
        }
        file << endl;
    }

    int log_size = min(10, (int)game_state.turn_log.size());
    file << log_size << endl;
    for (int i = 0; i < log_size; i++) {
        file << game_state.turn_log[game_state.turn_log.size() - log_size + i] << "|||";
    }
    file.close();
    ConsoleHelper::SetColor(6);
    cout << "Game state saved to " << filename << endl;
    ConsoleHelper::SetColor(8);
}

void load_game_state(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Failed to load game state from " << filename << endl;
        return;
    }
    lock_guard<mutex> lock(game_mutex);
    game_state.players.clear();
    game_state.turn_log.clear();
    next_client_id = 1;

    int version;
    file >> version;
    if (version != 2) {
        cerr << "Unsupported save file version (expected 2, got " << version << "). Aborting load." << endl;
        return;
    }

    file >> game_state.current_turn;
    file >> game_state.turn_duration_seconds;
    file >> game_state.is_active;
    int player_count;
    file >> player_count;

    for (int i = 0; i < player_count; i++) {
        Player player;
        file >> player.name
            >> player.hp
            >> player.max_hp
            >> player.x
            >> player.y
            >> player.is_admin
            >> player.can_move;

        size_t attr_count;
        file >> attr_count;
        for (size_t j = 0; j < attr_count; ++j) {
            string key, typeStr;
            file >> key >> typeStr;
            if (typeStr == "int") {
                int val; file >> val;
                player.setAttr(key, val);
            }
            else if (typeStr == "float") {
                float val; file >> val;
                player.setAttr(key, val);
            }
            else if (typeStr == "string") {
                string val; file >> val;
                player.setAttr(key, val);
            }
            else if (typeStr == "bool") {
                int val; file >> val;
                player.setAttr(key, (bool)val);
            }
            else {
                cerr << "Unknown attribute type: " << typeStr << ", skipping key " << key << endl;
                // Пропускаем до конца строки (можно просто читать строку, но упростим)
            }
        }

        int new_id = next_client_id++;
        player.id = new_id;
        game_state.players[new_id] = player;

        ConsoleHelper::SetColor(4);
        cout << "Loaded player: " << player.name
            << " (HP: " << player.hp << "/" << player.max_hp
            << ")" << endl;
        ConsoleHelper::SetColor(8);
    }

    int log_size;
    file >> log_size;
    file.ignore();
    for (int i = 0; i < log_size; i++) {
        string log_entry;
        getline(file, log_entry, '|');
        if (!log_entry.empty()) {
            game_state.turn_log.push_back(log_entry);
        }
        file.ignore(2);
    }
    file.close();
    ConsoleHelper::SetColor(6);
    cout << "Game state loaded from " << filename << ". " << player_count << " players restored." << endl;
    ConsoleHelper::SetColor(8);
}

void auto_save_thread() {
    while (true) {
        this_thread::sleep_for(chrono::minutes(15));
        if (game_state.is_active) {
            save_game_state("autosave.mcgsave");
            ConsoleHelper::SetColor(10);
            cout << "Auto-save completed" << endl;
            ConsoleHelper::SetColor(8);
        }
    }
}

// ---------- Lua functions implementation ----------
int lua_get_hp(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    lua_pushinteger(L, (it != game_state.players.end()) ? it->second.hp : 0);
    return 1;
}
int lua_set_hp(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    int hp = luaL_checkinteger(L, 2);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    if (it != game_state.players.end()) {
        it->second.hp = max(0, min(hp, it->second.max_hp));
    }
    return 0;
}
int lua_get_max_hp(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    lua_pushinteger(L, (it != game_state.players.end()) ? it->second.max_hp : 0);
    return 1;
}
int lua_set_max_hp(lua_State* L) {
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
int lua_get_x(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    lua_pushinteger(L, (it != game_state.players.end()) ? it->second.x : 0);
    return 1;
}
int lua_set_x(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    int x = luaL_checkinteger(L, 2);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    if (it != game_state.players.end()) it->second.x = x;
    return 0;
}
int lua_get_y(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    lua_pushinteger(L, (it != game_state.players.end()) ? it->second.y : 0);
    return 1;
}
int lua_set_y(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    if (it != game_state.players.end()) it->second.y = y;
    return 0;
}
int lua_set_can_move(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    bool can = lua_toboolean(L, 2) != 0;
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    if (it != game_state.players.end()) {
        it->second.can_move = can;
    }
    return 0;
}

int lua_get_can_move(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    bool can = (it != game_state.players.end()) ? it->second.can_move : true;
    lua_pushboolean(L, can);
    return 1;
}
int lua_get_player_name(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    lock_guard<mutex> lock(game_mutex);
    auto it = game_state.players.find(id);
    lua_pushstring(L, (it != game_state.players.end()) ? it->second.name.c_str() : "");
    return 1;
}
int lua_send_to_player(lua_State* L) {
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
int lua_broadcast(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    broadcast_message(msg, INVALID_SOCKET);
    return 0;
}
// Возвращает таблицу с ID всех игроков (индексы 1,2,3...)
int lua_get_all_players(lua_State* L) {
    lock_guard<mutex> lock(game_mutex);
    lua_newtable(L);
    int index = 1;
    for (const auto& pair : game_state.players) {
        lua_pushinteger(L, index++);
        lua_pushinteger(L, pair.first); // ID игрока
        lua_settable(L, -3);
    }
    return 1;
}
// Возвращает таблицу ID игроков в радиусе (манхэттенское расстояние) от точки (x,y)
int lua_get_players_in_radius(lua_State* L) {
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
        int dist = abs(dx) + abs(dy); // манхэттенское расстояние (можно заменить на sqrt для евклидова)
        if (dist <= radius) {
            lua_pushinteger(L, index++);
            lua_pushinteger(L, pair.first);
            lua_settable(L, -3);
        }
    }
    return 1;
}
// Возвращает расстояние между двумя точками (манхэттенское)
int lua_get_distance(lua_State* L) {
    int x1 = luaL_checkinteger(L, 1);
    int y1 = luaL_checkinteger(L, 2);
    int x2 = luaL_checkinteger(L, 3);
    int y2 = luaL_checkinteger(L, 4);
    int dist = abs(x1 - x2) + abs(y1 - y2);
    lua_pushinteger(L, dist);
    return 1;
}
int lua_get_attr(lua_State* L) {
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
int lua_set_attr(lua_State* L) {
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

void register_lua_functions() {
    lua_register(gLuaState, "get_hp", lua_get_hp);
    lua_register(gLuaState, "set_hp", lua_set_hp);
    lua_register(gLuaState, "get_max_hp", lua_get_max_hp);
    lua_register(gLuaState, "set_max_hp", lua_set_max_hp);
    lua_register(gLuaState, "get_x", lua_get_x);
    lua_register(gLuaState, "set_x", lua_set_x);
    lua_register(gLuaState, "get_y", lua_get_y);
    lua_register(gLuaState, "set_y", lua_set_y);
    lua_register(gLuaState, "set_can_move", lua_set_can_move);
    lua_register(gLuaState, "get_can_move", lua_get_can_move);
    lua_register(gLuaState, "get_player_name", lua_get_player_name);
    lua_register(gLuaState, "send_to_player", lua_send_to_player);
    lua_register(gLuaState, "broadcast", lua_broadcast);
    lua_register(gLuaState, "get_all_players", lua_get_all_players);
    lua_register(gLuaState, "get_players_in_radius", lua_get_players_in_radius);
    lua_register(gLuaState, "get_distance", lua_get_distance);
    lua_register(gLuaState, "get_attr", lua_get_attr);
    lua_register(gLuaState, "set_attr", lua_set_attr);
}

void LoadLuaScripts() {
    // Очищаем старые описания
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

    do {
        string filename = "actions/" + string(findData.cFileName);
        if (luaL_dofile(gLuaState, filename.c_str()) != LUA_OK) {
            cerr << "Lua error in " << filename << ": " << lua_tostring(gLuaState, -1) << endl;
            lua_pop(gLuaState, 1);
            continue;
        }

        // Извлекаем имя команды (имя файла без расширения .lua)
        string cmd_name = findData.cFileName;
        size_t dot = cmd_name.find_last_of('.');
        if (dot != string::npos) cmd_name = cmd_name.substr(0, dot);

        // Пытаемся получить описание через функцию get_description()
        lua_getglobal(gLuaState, "get_description");
        if (lua_isfunction(gLuaState, -1)) {
            if (lua_pcall(gLuaState, 0, 1, 0) == LUA_OK) {
                if (lua_isstring(gLuaState, -1)) {
                    const char* desc = lua_tostring(gLuaState, -1);
                    lock_guard<mutex> lock(lua_desc_mutex);
                    lua_command_descriptions[cmd_name] = desc;
                } else {
                    lock_guard<mutex> lock(lua_desc_mutex);
                    lua_command_descriptions[cmd_name] = "";
                }
                lua_pop(gLuaState, 1);
            } else {
                const char* err = lua_tostring(gLuaState, -1);
                cerr << "Error calling get_description() in " << filename << ": " << err << endl;
                lua_pop(gLuaState, 1);
                lock_guard<mutex> lock(lua_desc_mutex);
                lua_command_descriptions[cmd_name] = "";
            }
        } else {
            lua_pop(gLuaState, 1); // убираем nil
            lock_guard<mutex> lock(lua_desc_mutex);
            lua_command_descriptions[cmd_name] = "";
        }

        cout << "Loaded: " << filename << endl;
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
}

// ========== КОМАНДЫ ОПИСАНИЯ И ПРАВИЛ ==========

void handle_edit_description(SOCKET client_sock, const string& command) {
    // команда вида /edit_description Текст с \n для переноса
    string text = command.substr(17); // длина "/edit_description " (17)
    if (text.empty()) {
        string err = "[ERROR] Usage: /edit_description <text with \\n for newline>\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    // Заменяем "\\n" (два символа) на реальный символ перевода строки
    size_t pos = 0;
    while ((pos = text.find("\\n", pos)) != string::npos) {
        text.replace(pos, 2, "\n");
        pos += 1; // пропускаем только что вставленный символ
    }
    {
        lock_guard<mutex> lock(server_info_mutex);
        server_description = text;
    }
    send(client_sock, "Server description updated.\n", 28, 0);
}

void handle_edit_rules(SOCKET client_sock, const string& command) {
    string text = command.substr(12); // "/edit_rules " длина 12
    if (text.empty()) {
        string err = "[ERROR] Usage: /edit_rules <text with \\n for newline>\n";
        send(client_sock, err.c_str(), (int)err.size(), 0);
        return;
    }
    size_t pos = 0;
    while ((pos = text.find("\\n", pos)) != string::npos) {
        text.replace(pos, 2, "\n");
        pos += 1;
    }
    {
        lock_guard<mutex> lock(server_info_mutex);
        server_rules = text;
    }
    send(client_sock, "Server rules updated.\n", 22, 0);
}

void handle_description(SOCKET client_sock) {
    lock_guard<mutex> lock(server_info_mutex);
    if (server_description.empty()) {
        send(client_sock, "No description set.\n", 20, 0);
    }
    else {
        send(client_sock, server_description.c_str(), (int)server_description.size(), 0);
        send(client_sock, "\n", 1, 0);
    }
}

void handle_rules(SOCKET client_sock) {
    lock_guard<mutex> lock(server_info_mutex);
    if (server_rules.empty()) {
        send(client_sock, "No rules set.\n", 14, 0);
    }
    else {
        send(client_sock, server_rules.c_str(), (int)server_rules.size(), 0);
        send(client_sock, "\n", 1, 0);
    }
}

void handle_client(SOCKET client_sock) {
    char buffer[1024];
    cout << "Client connected: socket " << client_sock << endl;

    if (!server_description.empty()) {
        string msg = "[c8]------<====   Description   ====>-------\n" + server_description + "\n----------------------------------------[/c8]\n";
        send(client_sock, msg.c_str(), (int)msg.size(), 0);
    }

    string auth_msg = "Authentication required.\nFormat: [c2]AUTH|username|configure password[/c2]\n========================================\n";
    send(client_sock, auth_msg.c_str(), static_cast<int>(auth_msg.length()), 0);

    bool authenticated = false;
    bool is_admin = false;
    string username;

    while (!authenticated) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            cout << "Client disconnected during auth" << endl;
            closesocket(client_sock);
            client_count--;
            return;
        }
        buffer[bytes] = '\0';
        string packet(buffer);
        size_t pos1 = packet.find('|');
        if (pos1 != string::npos && packet.substr(0, pos1) == "AUTH") {
            string rest = packet.substr(pos1 + 1);
            size_t pos2 = rest.find('|');
            if (pos2 != string::npos) {
                username = rest.substr(0, pos2);
                string password = rest.substr(pos2 + 1);
                if (password == Password) {
                    is_admin = true;
                    admin_clients[client_sock] = true;
                    cout << "Admin user '" << username << "' connected" << endl;
                }
                int client_id = next_client_id++;
                client_info[client_sock] = make_pair(username, client_id);
                authenticated = true;
                string auth_success = "OK|Welcome " + username;
                if (is_admin) auth_success += " (ADMIN)";
                auth_success += "! Your ID: " + to_string(client_id) + "\nType /help for commands\n";
                send(client_sock, auth_success.c_str(), static_cast<int>(auth_success.length()), 0);
                cout << "User '" << username << "' (ID: " << client_id << ") ";
                if (is_admin) cout << "[ADMIN] ";
                cout << "authenticated" << endl;
                string join_msg = "[SERVER] " + username;
                if (is_admin) join_msg += " [ADMIN]";
                join_msg += " joined the chat";
                broadcast_message(join_msg, client_sock);
                {
                    lock_guard<mutex> lock(game_mutex);
                    Player player;
                    player.name = username;
                    player.id = client_id;
                    player.is_admin = is_admin;
                    player.sock = client_sock;
                    apply_default_attrs(player);
                    bool player_exists = false;
                    for (auto& pair : game_state.players) {
                        if (pair.second.name == username) {
                            pair.second.id = client_id;
                            player_exists = true;
                            break;
                        }
                    }
                    if (!player_exists) {
                        game_state.players[client_id] = player;
                    }
                }
            }
        }
        if (!authenticated) {
            string error = "[bgB][c4][ERROR][/c4][/bgB]|Invalid format.\n[c4]Use: AUTH|username|password[/c4]\n";
            send(client_sock, error.c_str(), static_cast<int>(error.length()), 0);
        }
    }

    if (!server_rules.empty()) {
        string msg = "=====--- Server Rules ---=====\n" + server_rules + "\n============================\n";
        send(client_sock, msg.c_str(), (int)msg.size(), 0);
    }

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        string message(buffer);
        if (!message.empty()) {
            if (message[0] == '/') {
                if (message == "/help") {
                    string help_msg = "\n[COMMAND]\n";
                    help_msg += "\n[c2][bgC]=== Available Commands ===[/bgC][/c2]\n";
                    help_msg += "/help - Show this message\n";
                    help_msg += "/list - List all connected clients\n";
                    help_msg += "/description - Show server description\n";
                    help_msg += "/rules - Show server rules\n";
                    {
                        lock_guard<mutex> lock(game_mutex);
                        if (game_state.players.find(client_info[client_sock].second) != game_state.players.end()) {
                            help_msg += "==================--   Player's   --================\n";
                            help_msg += "/move [direction] - [c8]Move (north, south, east, west)[/c8]\n";
                            help_msg += "/skip - [c8]Skip your turn[/c8]\n";
                            help_msg += "/status - [c8]Check your status[/c8]\n";
                            help_msg += "/map - [c8]Show game map[/c8]\n";
                            help_msg += "/ready - [c8]Mark yourself as ready[/c8]\n";
                            help_msg += "/unready - [c8]Mark yourself as not ready[/c8]\n";
                            help_msg += "/time_remaining - [c8]Check how much time left[/c8]\n";
                        }
                    }

                    // ----- Динамические Lua-команды -----
                    vector<string> lua_cmds;
                    WIN32_FIND_DATAA findData;
                    HANDLE hFind = FindFirstFileA("actions/*.lua", &findData);
                    if (hFind != INVALID_HANDLE_VALUE) {
                        do {
                            string fname = findData.cFileName;
                            size_t dot = fname.find_last_of('.');
                            if (dot != string::npos) fname = fname.substr(0, dot);
                            lua_cmds.push_back(fname);
                        } while (FindNextFileA(hFind, &findData));
                        FindClose(hFind);
                    }
                    if (!lua_cmds.empty()) {
                        help_msg += "===--- Dynamic (Lua) Commands ---===\n";
                        for (const auto& cmd : lua_cmds) {
                            help_msg += "/" + cmd;
                            {
                                lock_guard<mutex> lock(lua_desc_mutex);
                                auto it = lua_command_descriptions.find(cmd);
                                if (it != lua_command_descriptions.end() && !it->second.empty()) {
                                    help_msg += " - " + it->second;
                                }
                            }
                            help_msg += "\n";
                        }
                    }
                    if (is_admin) {
                        help_msg += "=====--  Admin's  --=====\n";
                        help_msg += "/kick [id] - Kick a client\n";
                        help_msg += "/name[new_name] - Change server name\n";
                        help_msg += "/max [number] - Change max clients\n";
                        help_msg += "/info - Show server info\n";
                        help_msg += "/edit_description <text with \\n> - Set server description (use \\n for newline)\n";
                        help_msg += "/edit_rules <text with \\n> - Set server rules (use \\n for newline)\n";
                        help_msg += "/clients - Show detailed client info\n";
                        help_msg += "/start_game - Start the game\n";
                        help_msg += "/pause_game - Pause the game\n";
                        help_msg += "/set_turn_time[seconds] - Set turn duration\n";
                        help_msg += "/end_turn - Force end current turn\n";
                        help_msg += "/set_can_move[id][true / false] - Allow or forbid movement for a player\n";
                        help_msg += "/save[filename] - Save game state\n";
                        help_msg += "/load [filename] - Load game state\n";
                        help_msg += "/reload_scripts - Reload Lua actions\n";
                        help_msg += "/add_item - add item\n";
                        help_msg += "/set_hp [target_id] [hp] - set HP to choosen target (if setted HP > then max HP of target it would change to max)\n";
                        help_msg += "/set_attr_all <attr_name> <value> - set attribute for ALL current players\n";
                        help_msg += "/set_attr <target_id> <attr_name> <value> - set attribute for specific player\n";
                        help_msg += "/get_attr <target_id> <attr_name> - get attribute value\n";
                        help_msg += "/has_attr <target_id> <attr_name> - check if attribute exists\n";
                        help_msg += "/remove_attr <target_id> <attr_name> - remove attribute from specific player\n";
                        help_msg += "/remove_attr_all <attr_name> - remove attribute from ALL players\n";
                        help_msg += "/default_attr_add <attr_name> <value> - add/modify default attribute for future players\n";
                        help_msg += "/default_attr_remove <attr_name> - remove default attribute\n";
                        help_msg += "/default_attr_list - list all default attributes\n";
                        help_msg += "/sync_default_attrs - apply current default attributes to all existing players\n";
                        help_msg += "/list_attrs [target_id] - list all attributes of a player (default: yourself)\n";
                    }
                    help_msg += "====-----          -----====\n";
                    send(client_sock, help_msg.c_str(), static_cast<int>(help_msg.length()), 0);
                    continue;
                }
                else if (message == "/list") {
                    string list_msg = "\n=== Connected Clients (" + to_string(client_count) + ") ===\n";
                    for (auto& client : clients) {
                        if (client_info.find(client) != client_info.end()) {
                            auto& info = client_info[client];
                            list_msg += "ID: " + to_string(info.second) + " | Name: " + info.first;
                            if (admin_clients[client]) list_msg += " [ADMIN]";
                            {
                                lock_guard<mutex> lock(game_mutex);
                                if (game_state.players.find(info.second) != game_state.players.end()) {
                                    auto& player = game_state.players[info.second];
                                    list_msg += " | HP: " + to_string(player.hp) + "/" + to_string(player.max_hp);
                                }
                            }
                            list_msg += "\n";
                        }
                    }
                    list_msg += "==============================\n";
                    send(client_sock, list_msg.c_str(), static_cast<int>(list_msg.length()), 0);
                    continue;
                }
                else if (message == "/description") {
                    handle_description(client_sock);
                    continue;
                }
                else if (message == "/rules") {
                    handle_rules(client_sock);
                    continue;
                }
                else if (is_admin) {
                    if (message.length() > 6 && message.substr(0, 5) == "/kick") {
                        try {
                            int kick_id = stoi(message.substr(6));
                            bool found = false;
                            for (auto& client : clients) {
                                if (client_info.find(client) != client_info.end() && client_info[client].second == kick_id) {
                                    if (!admin_clients[client]) {
                                        string kick_msg = "[SERVER] You have been kicked by admin";
                                        send(client, kick_msg.c_str(), static_cast<int>(kick_msg.length()), 0);
                                        closesocket(client);
                                        string notify = "[SERVER] Client ID " + to_string(kick_id) + " was kicked by admin";
                                        broadcast_message(notify, INVALID_SOCKET);
                                        found = true;
                                        break;
                                    }
                                    else {
                                        string error = "[ERROR]|Cannot kick another admin\n";
                                        send(client_sock, error.c_str(), static_cast<int>(error.length()), 0);
                                        found = true;
                                    }
                                }
                            }
                            if (!found) {
                                string error = "[ERROR]|Client not found\n";
                                send(client_sock, error.c_str(), static_cast<int>(error.length()), 0);
                            }
                        }
                        catch (...) {
                            string error = "[ERROR]|Invalid client ID\n";
                            send(client_sock, error.c_str(), static_cast<int>(error.length()), 0);
                        }
                        continue;
                    }
                    else if (message.length() > 6 && message.substr(0, 5) == "/name") {
                        Name = message.substr(6);
                        string notify = "[SERVER] Server name changed to: " + Name;
                        broadcast_message(notify, INVALID_SOCKET);
                        continue;
                    }
                    else if (message.length() > 5 && message.substr(0, 4) == "/max") {
                        try {
                            int new_max = stoi(message.substr(5));
                            if (new_max > 0 && new_max < 1000) {
                                max_clients = new_max;
                                string notify = "[SERVER] Max clients changed to: " + to_string(max_clients);
                                broadcast_message(notify, INVALID_SOCKET);
                            }
                            else {
                                string error = "[ERROR]|Max clients must be between 1 and 1000\n";
                                send(client_sock, error.c_str(), static_cast<int>(error.length()), 0);
                            }
                        }
                        catch (...) {
                            string error = "[ERROR]|Invalid number\n";
                            send(client_sock, error.c_str(), static_cast<int>(error.length()), 0);
                        }
                        continue;
                    }
                    else if (message == "/info") {
                        int minutes = static_cast<int>(game_state.turn_duration_seconds / 60);
                        int seconds = static_cast<int>(game_state.turn_duration_seconds - (minutes * 60));
                        string info_msg = "\n=== Server Information ===\n";
                        info_msg += "Name: " + Name + "\nPort: " + to_string(PORT) + "\n";
                        info_msg += "Max clients: " + to_string(max_clients) + "\n";
                        info_msg += "Connected clients: " + to_string(client_count) + "\n";
                        info_msg += "Config password: " + Password + "\n";
                        info_msg += "Game active: " + string(game_state.is_active ? "Yes" : "No") + "\n";
                        info_msg += "Current turn: " + to_string(game_state.current_turn) + "\n";
                        info_msg += "Turn duration: " + to_string(minutes) + "m " + to_string(seconds) + "s\n";
                        info_msg += "==========================\n";
                        send(client_sock, info_msg.c_str(), static_cast<int>(info_msg.length()), 0);
                        continue;
                    }
                    else if (message == "/clients") {
                        string detailed_msg = "\n=== Detailed Client Information ===\n";
                        for (auto& client : clients) {
                            if (client_info.find(client) != client_info.end()) {
                                auto& info = client_info[client];
                                detailed_msg += "Socket: " + to_string(client) + " | ID: " + to_string(info.second) + " | Name: " + info.first;
                                if (admin_clients[client]) detailed_msg += " [ADMIN]";
                                detailed_msg += "\n";
                            }
                        }
                        detailed_msg += "===================================\n";
                        send(client_sock, detailed_msg.c_str(), static_cast<int>(detailed_msg.length()), 0);
                        continue;
                    }
                    else if (message == "/start_game") {
                        game_state.is_active = true;
                        game_state.turn_start_time = time(nullptr);
                        game_state.current_turn = 1;
                        broadcast_message("=== GAME STARTED ===\nTurn duration: " + to_string(game_state.turn_duration_seconds / 60) + " minutes and " + to_string(game_state.turn_duration_seconds - ((game_state.turn_duration_seconds / 60)*60)) + " seconds\n", INVALID_SOCKET);
                        continue;
                    }
                    else if (message == "/pause_game") {
                        game_state.is_active = false;
                        broadcast_message("Game paused by admin.", INVALID_SOCKET);
                        continue;
                    }
                    else if (message.length() > 14 && message.substr(0, 14) == "/set_turn_time") {
                        try {
                            int secTime = stoi(message.substr(15));
                            game_state.turn_duration_seconds = secTime;
                            int minutes = static_cast<int>(game_state.turn_duration_seconds / 60);
                            int seconds = static_cast<int>(game_state.turn_duration_seconds - (minutes * 60));
                            broadcast_message("Turn duration set to " + to_string(minutes) + "m " + to_string(seconds) + "s", INVALID_SOCKET);
                        }
                        catch (...) {
                            string error = "[ERROR]|Invalid number\n";
                            send(client_sock, error.c_str(), static_cast<int>(error.length()), 0);
                        }
                        continue;
                    }
                    else if (message.substr(0, 13) == "/set_can_move") {
                        istringstream iss(message.substr(14));
                        int target_id;
                        string val;
                        if (!(iss >> target_id >> val)) {   // <-- ЭТА ПРОВЕРКА КРИТИЧНА
                            send(client_sock, "Usage: /set_can_move <player_id> <true/false>\n", 48, 0);
                            continue;
                        }
                        bool can_move;
                        if (val == "true") can_move = true;
                        else if (val == "false") can_move = false;
                        else {
                            send(client_sock, "Value must be true or false\n", 29, 0);
                            continue;
                        }
                        lock_guard<mutex> lock(game_mutex);
                        auto it = game_state.players.find(target_id);
                        if (it != game_state.players.end()) {
                            it->second.can_move = can_move;   // присваиваем поле, а не атрибут!
                            string msg = "Player " + to_string(target_id) + " can_move set to " + (can_move ? "true" : "false") + "\n";
                            send(client_sock, msg.c_str(), msg.size(), 0);
                        }
                        else {
                            send(client_sock, "Player not found\n", 18, 0);
                        }
                        continue;
                        }
                    else if (message == "/end_turn") {
                        process_turn_end();
                        continue;
                    }
                    else if (message.length() > 5 && message.substr(0, 5) == "/save") {
                        string filename = message.substr(6);
                        if (filename.empty()) filename = "game_save.rpg";
                        thread(save_game_state, filename).detach();
                        send(client_sock, "Saving game...\n", 15, 0);
                        continue;
                    }
                    else if (message.length() > 5 && message.substr(0, 5) == "/load") {
                        string filename = message.substr(6);
                        if (filename.empty()) filename = "game_save.rpg";
                        thread(load_game_state, filename).detach();
                        send(client_sock, "Loading game...\n", 16, 0);
                        continue;
                    }
                    else if (message.substr(0, 13) == "/set_attr_all") {
                        handle_set_attr_all(client_sock, message);
                        continue;
                    }
                    else if (message.substr(0, 9) == "/set_attr") {
                        handle_set_attr(client_sock, message, client_info[client_sock].second, is_admin);
                        continue;
                    }
                    else if (message.substr(0, 9) == "/get_attr") {
                        handle_get_attr(client_sock, message);
                        continue;
                    }
                    else if (message.substr(0, 9) == "/has_attr") {
                        handle_has_attr(client_sock, message);
                        continue;
                    }
                    else if (message.substr(0, 16) == "/remove_attr_all") {
                        handle_remove_attr_all(client_sock, message);
                        continue;
                    }
                    else if (message.substr(0, 11) == "/remove_attr") {
                        handle_remove_attr(client_sock, message, client_info[client_sock].second, is_admin);
                        continue;
                    }
                    else if (message.substr(0, 17) == "/default_attr_add") {
                        handle_default_attr_add(client_sock, message);
                        continue;
                    }
                    else if (message.substr(0, 20) == "/default_attr_remove") {
                        handle_default_attr_remove(client_sock, message);
                        continue;
                        }
                    else if (message.substr(0, 17) == "/default_attr_list") {
                        handle_default_attr_list(client_sock);
                        continue;
                    }
                    else if (message == "/sync_default_attrs") {
                        handle_sync_default_attrs(client_sock);
                        continue;
                    }
                    else if (message.substr(0, 11) == "/list_attrs") {
                        handle_list_attrs(client_sock, message, client_info[client_sock].second);
                        continue;
                    }
                    else if (message.substr(0, 17) == "/edit_description") {
                        handle_edit_description(client_sock, message);
                        continue;
}
                    else if (message.substr(0, 12) == "/edit_rules") {
                        handle_edit_rules(client_sock, message);
                        continue;
                    }
                }
                int player_id = client_info[client_sock].second;
                if (game_state.players.find(player_id) != game_state.players.end()) {
                    process_game_command(client_sock, message, player_id, is_admin);
                    continue;
                }
                string error = "[ERROR]|Unknown command. Type /help for available commands\n";
                send(client_sock, error.c_str(), static_cast<int>(error.length()), 0);
                continue;
            }
            auto& info = client_info[client_sock];
            string full_msg = "[" + to_string(info.second) + "] " + info.first;
            if (is_admin) full_msg += " [ADMIN]";
            full_msg += ": " + message;
            cout << "Message: " << full_msg << endl;
            full_msg = "[CHAT] " + full_msg;
            broadcast_message(full_msg, client_sock);
        }
    }

    auto it = find(clients.begin(), clients.end(), client_sock);
    if (it != clients.end()) {
        clients.erase(it);
        client_count--;
    }
    if (admin_clients.find(client_sock) != admin_clients.end()) admin_clients.erase(client_sock);
    {
        lock_guard<mutex> lock(game_mutex);
        int player_id = client_info[client_sock].second;
        if (game_state.players.find(player_id) != game_state.players.end()) game_state.players.erase(player_id);
    }
    if (client_info.find(client_sock) != client_info.end()) {
        string leave_msg = "[SERVER] " + client_info[client_sock].first + " left the chat";
        broadcast_message(leave_msg, INVALID_SOCKET);
        client_info.erase(client_sock);
    }
    closesocket(client_sock);
    cout << "Client disconnected. Total: " << client_count << endl;
}

bool is_valid_port(const string& port_str, int& port_out) {
    if (port_str.empty()) return false;
    try {
        size_t pos;
        int port = stoi(port_str, &pos);
        // Проверяем, что вся строка была использована и порт в допустимом диапазоне
        if (pos != port_str.length()) return false;
        if (port < 1 || port > 65535) return false;
        port_out = port;
        return true;
    }
    catch (...) {
        return false;
    }
}

int main() {
    cout << "=== Cross-platform RPG Game Server (Windows) ===" << endl;
    if (!network_init()) return 1;
    cout << "Enter max clients (recommended limit 200): ";
    cin >> max_clients;
    cout << "Enter configuration password: ";
    cin >> Password;
    cout << "Enter server name: ";
    cin >> Name;
    cout << "Enter server port (must be 1-65535): ";
    int Port;
    string S_Port;
    cin >> S_Port;
    if (!is_valid_port(S_Port, Port)) {
        cerr << "\n[ERROR] Invalid server port. Using default " << PORT << "\n";
        Port = PORT;
    }
    PORT = Port;
    cout << endl;
    char load_choice;
    cout << "Load saved game? (y/n): ";
    cin >> load_choice;
    if (load_choice == 'y' || load_choice == 'Y') {
        string save_file;
        cout << "Enter save file name (default: game_save.mcgsave): ";
        cin >> save_file;
        if (save_file.empty()) save_file = "game_save.mcgsave";
        load_game_state(save_file);
        thread(auto_save_thread).detach();
    }
    cin.ignore();
    thread(game_timer_thread).detach();

    SOCKET server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == INVALID_SOCKET) {
        cerr << "Socket creation failed: " << WSAGetLastError() << endl;
        network_cleanup();
        return 1;
    }

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
        cerr << "Set socket option failed: " << WSAGetLastError() << endl;
        closesocket(server_sock);
        network_cleanup();
        return 1;
    }

    if (::bind(server_sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Bind failed: " << WSAGetLastError() << endl;
        cerr << "Make sure port " << PORT << " is available." << endl;
        closesocket(server_sock);
        network_cleanup();
        return 1;
    }

    if (listen(server_sock, SOMAXCONN) == SOCKET_ERROR) {
        cerr << "Listen failed: " << WSAGetLastError() << endl;
        closesocket(server_sock);
        network_cleanup();
        return 1;
    }

    int minutes = static_cast<int>(game_state.turn_duration_seconds / 60);
    int seconds = static_cast<int>(game_state.turn_duration_seconds - (minutes * 60));
    cout << "\n=== Server Information ===" << endl;
    cout << "Name: " << Name << endl;
    cout << "Platform: Windows" << endl;
    cout << "Port: " << PORT << endl;
    cout << "Max clients: " << max_clients << endl;
    cout << "Server IP: 0.0.0.0 (all interfaces)" << endl;
    cout << "Localhost: 127.0.0.1:" << PORT << endl;
    cout << "Game system: RPG Turn-based" << endl;
    cout << "Default turn time: " << to_string(minutes) << "m " << to_string(seconds) << "s" << endl;
    cout << "Waiting for connections..." << endl;
    cout << "Press Ctrl+C to stop server" << endl;
    cout << "==========================\n" << endl;

    gLuaState = luaL_newstate();
    if (!gLuaState) {
        cerr << "Failed to create Lua state" << endl;
        return 1;
    }
    luaL_openlibs(gLuaState);   // открывает стандартные библиотеки Lua (math, string, table и т.д.)
    register_lua_functions();
    LoadLuaScripts();

    while (true) {
        sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        SOCKET client_sock = accept(server_sock, (sockaddr*)&client_addr, &client_len);
        if (client_sock == INVALID_SOCKET) {
            cerr << "Accept failed: " << WSAGetLastError() << endl;
            continue;
        }
        if (client_count >= max_clients) {
            string error = "[ERROR]|Server is full. Max: " + to_string(max_clients);
            send(client_sock, error.c_str(), static_cast<int>(error.length()), 0);
            closesocket(client_sock);
            cout << "Connection rejected: server full (" << max_clients << " clients)" << endl;
            continue;
        }
        string welcome_msg = "\n[bgF][c0]====-- Connected to " + Name + " RPG Server --====[/c0][/bgF]";
        send(client_sock, welcome_msg.c_str(), static_cast<int>(welcome_msg.length()), 0);
        clients.push_back(client_sock);
        client_count++;
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        cout << "New connection from " << client_ip << ":" << ntohs(client_addr.sin_port) << endl;
        cout << "Total clients: " << client_count << "/" << max_clients << endl;
        thread client_thread(handle_client, client_sock);
        client_thread.detach();
    }

    closesocket(server_sock);
    network_cleanup();
    return 0;
}