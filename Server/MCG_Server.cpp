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
#include <unordered_set>
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
    int start_x;  // пред. позиция для отката хода
    int start_y;


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

struct Tile {
    int id = 0;
    bool walkable = true;
    string on_enter;
    string on_exit;
    string on_step;
    string display;   // внешний вид (один символ, пара символов или несколько строк)
};

// Глобальные данные карты
vector<vector<int>> world_map;  // world_map[y][x] = id тайла
int map_width = 0, map_height = 0;
int map_center_x = 0;
int map_center_y = 0;
map<int, Tile> tile_types;      // соответствие id -> свойства тайла

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
// Управление Lua-командами
unordered_set<string> available_lua_commands;  // все загруженные скрипты (имена)
unordered_set<string> active_lua_commands;     // активные в данный момент
mutex lua_commands_mutex;                      // для потокобезопасного доступа
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

int Random(int max, int min) {
    return rand() % (max - min + 1) + min;
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
// Функции для работы с картой и тайлами
void load_world_map(const string& filename);
void save_world_map(const string& filename);
bool is_walkable(int x, int y);
void call_tile_function(const string& func_name, int player_id, int x, int y, int tile_id);
void on_player_enter_tile(Player& player, int old_x, int old_y, int new_x, int new_y);
void set_tile(int x, int y, int new_id);

bool find_nearest_walkable(int tx, int ty, int& out_x, int& out_y) {
    int best_dist = INT_MAX;
    bool found = false;
    // Логические границы карты:
    int min_log_x = -map_center_x;
    int max_log_x = map_width - 1 - map_center_x;
    int min_log_y = -map_center_y;
    int max_log_y = map_height - 1 - map_center_y;
    for (int y = min_log_y; y <= max_log_y; ++y) {
        for (int x = min_log_x; x <= max_log_x; ++x) {
            if (is_walkable(x, y)) {
                int dist = abs(x - tx) + abs(y - ty);
                if (dist < best_dist) {
                    best_dist = dist;
                    out_x = x;
                    out_y = y;
                    found = true;
                }
            }
        }
    }
    return found;
}
// Корректирует позицию игрока, если текущая невалидна.
// Возвращает true, если позиция была изменена.
bool correct_player_position(Player& player) {
    int old_x = player.x, old_y = player.y;
    if (is_walkable(old_x, old_y)) {
        return false; // позиция валидна
    }
    // Ищем ближайшую проходимую клетку к центру (0,0)
    int new_x, new_y;
    if (find_nearest_walkable(0, 0, new_x, new_y)) {
        player.x = new_x;
        player.y = new_y;
        string msg = "[SYSTEM] Your position was invalid. You have been moved to (" +
            to_string(new_x) + "," + to_string(new_y) + ").\n";
        send(player.sock, msg.c_str(), msg.size(), 0);
        return true;
    }
    return false;
}

// Корректирует позиции всех игроков, у которых текущая позиция невалидна.
void correct_all_players_positions() {
    lock_guard<mutex> lock(game_mutex);
    for (auto& [id, player] : game_state.players) {
        correct_player_position(player);
    }
}

void load_world_map(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Failed to load world map from " << filename << ". Creating default 10x10 map." << endl;
        world_map.clear();
        world_map.push_back({ 1 });
        map_width = 10;
        map_height = 10;
        map_center_x = (map_width - 1) / 2;
        map_center_y = (map_height - 1) / 2;
        return;
    }

    world_map.clear();
    string line;
    while (getline(file, line)) {
        if (line.empty()) continue;
        vector<int> row;
        size_t pos = 0;
        string token;
        while ((pos = line.find(';')) != string::npos) {
            token = line.substr(0, pos);
            if (!token.empty()) {
                row.push_back(stoi(token));
            }
            line.erase(0, pos + 1);
        }
        if (!row.empty())
            world_map.push_back(row);
    }
    file.close();

    if (world_map.empty()) {
        world_map.push_back({ 1 });
    }

    map_height = world_map.size();
    map_width = world_map[0].size();
    map_center_x = (map_width - 1) / 2;
    map_center_y = (map_height - 1) / 2;

    // Создаём тайлы для неизвестных id
    for (int y = 0; y < map_height; ++y) {
        for (int x = 0; x < map_width; ++x) {
            int tid = world_map[y][x];
            if (tile_types.find(tid) == tile_types.end()) {
                Tile new_tile;
                new_tile.id = tid;
                new_tile.walkable = true;
                new_tile.display = "?";   // стандартный маркер
                tile_types[tid] = new_tile;
                cout << "Auto-created tile for id " << tid << endl;
            }
        }
    }

    // После map_width, map_height и пересчёта map_center_x, map_center_y проверяем правильны ли позиции игроков
    correct_all_players_positions();

    ConsoleHelper::SetColor(10);
    cout << "World map loaded: " << map_width << "x" << map_height << endl;
    ConsoleHelper::SetColor(8);
}

// Установить тайл в логических координатах (x, y)
void set_tile(int x, int y, int new_id) {
    int abs_x = map_center_x + x;
    int abs_y = map_center_y - y;
    if (abs_x < 0 || abs_x >= map_width || abs_y < 0 || abs_y >= map_height) return;
    if (tile_types.find(new_id) == tile_types.end()) {
        Tile new_tile;
        new_tile.id = new_id;
        new_tile.walkable = true;
        new_tile.display = "?";
        tile_types[new_id] = new_tile;
        cout << "Auto-created tile id " << new_id << endl;
    }
    world_map[abs_y][abs_x] = new_id;
    correct_all_players_positions();
}
// Сохранить текущую карту в файл (в том же формате, что и World.txt)
void save_world_map(const string& filename) {
    ofstream file(filename);
    if (!file) {
        cerr << "Failed to save world map to " << filename << endl;
        return;
    }
    for (int y = 0; y < map_height; ++y) {
        for (int x = 0; x < map_width; ++x) {
            file << world_map[y][x];
            if (x != map_width - 1) file << ";";
        }
        file << ";\n";  // каждая строка заканчивается точкой с запятой
    }
    file.close();
    ConsoleHelper::SetColor(10);
    cout << "World map saved to " << filename << endl;
    ConsoleHelper::SetColor(8);
}

// Проверка, можно ли встать на клетку (x, y)
bool is_walkable(int x, int y) {   // x, y – логические координаты (центр 0,0)
    int abs_x = map_center_x + x;
    int abs_y = map_center_y - y;   // потому что y север, а в массиве y=0 верх
    if (abs_x < 0 || abs_x >= map_width || abs_y < 0 || abs_y >= map_height)
        return false;
    int tid = world_map[abs_y][abs_x];
    auto it = tile_types.find(tid);
    if (it == tile_types.end()) return false;
    return it->second.walkable;
}

// Вызов Lua-функции, привязанной к тайлу (on_enter/on_exit/on_step)
void call_tile_function(const string& func_name, int player_id, int x, int y, int tile_id) {
    if (func_name.empty()) return;
    lua_getglobal(gLuaState, func_name.c_str());
    if (lua_isfunction(gLuaState, -1)) {
        lua_pushinteger(gLuaState, player_id);
        lua_pushinteger(gLuaState, x);
        lua_pushinteger(gLuaState, y);
        lua_pushinteger(gLuaState, tile_id);
        if (lua_pcall(gLuaState, 4, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(gLuaState, -1);
            cerr << "Error calling tile function '" << func_name << "': " << err << endl;
            lua_pop(gLuaState, 1);
        }
    }
    else {
        lua_pop(gLuaState, 1);
    }
}

// Обработка смены тайла игроком (вызов on_exit старого и on_enter нового)
void on_player_enter_tile(Player& player, int old_x, int old_y, int new_x, int new_y) {
    // Выход со старого тайла
    int old_abs_x = map_center_x + old_x;
    int old_abs_y = map_center_y - old_y;
    if (old_abs_x >= 0 && old_abs_x < map_width && old_abs_y >= 0 && old_abs_y < map_height) {
        int old_tile_id = world_map[old_abs_y][old_abs_x];
        auto it = tile_types.find(old_tile_id);
        if (it != tile_types.end() && !it->second.on_exit.empty()) {
            call_tile_function(it->second.on_exit, player.id, old_x, old_y, old_tile_id);
        }
    }
    // Вход на новый тайл
    int new_abs_x = map_center_x + new_x;
    int new_abs_y = map_center_y - new_y;
    if (new_abs_x >= 0 && new_abs_x < map_width && new_abs_y >= 0 && new_abs_y < map_height) {
        int new_tile_id = world_map[new_abs_y][new_abs_x];
        auto it = tile_types.find(new_tile_id);
        if (it != tile_types.end() && !it->second.on_enter.empty()) {
            call_tile_function(it->second.on_enter, player.id, new_x, new_y, new_tile_id);
        }
    }
}
void save_tiles(const string& filename) {
    ofstream file(filename);
    if (!file) {
        cerr << "Failed to save tiles to " << filename << endl;
        return;
    }
    file << tile_types.size() << endl;
    for (const auto& [id, tile] : tile_types) {
        // Экранируем переводы строк в display
        string display_escaped = tile.display;
        size_t pos = 0;
        while ((pos = display_escaped.find('\n', pos)) != string::npos) {
            display_escaped.replace(pos, 1, "\\n");
            pos += 2;
        }
        file << id << " " << tile.walkable << " "
            << tile.on_enter << " " << tile.on_exit << " " << tile.on_step << " "
            << display_escaped << endl;
    }
    file.close();
    cout << "Tiles saved to " << filename << endl;
}

void load_tiles(const string& filename) {
    ifstream file(filename);
    if (!file) {
        cerr << "Failed to load tiles from " << filename << endl;
        return;
    }
    size_t count;
    file >> count;
    tile_types.clear();
    for (size_t i = 0; i < count; ++i) {
        Tile tile;
        string display_escaped;
        file >> tile.id >> tile.walkable >> tile.on_enter >> tile.on_exit >> tile.on_step >> display_escaped;
        // Восстанавливаем переводы строк
        string display;
        size_t pos = 0;
        while ((pos = display_escaped.find("\\n", pos)) != string::npos) {
            display += display_escaped.substr(0, pos) + "\n";
            display_escaped.erase(0, pos + 2);
            pos = 0;
        }
        display += display_escaped;
        tile.display = display;
        tile_types[tile.id] = tile;
    }
    file.close();
    cout << "Tiles loaded from " << filename << endl;
}

char get_tile_char(int tile_id) {
    auto it = tile_types.find(tile_id);
    if (it == tile_types.end()) return '?';
    const string& disp = it->second.display;
    if (disp.empty()) return '?';
    return disp[0];   // первый символ первой строки
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
        if (p.last_action != ActionType::MOVE) {
            int tid = world_map[p.y][p.x];
            auto it = tile_types.find(tid);
            if (it != tile_types.end() && !it->second.on_step.empty()) {
                call_tile_function(it->second.on_step, p.id, p.x, p.y, tid);
            }
        }
        int abs_x = map_center_x + p.x;
        int abs_y = map_center_y - p.y;
        int tid = world_map[abs_y][abs_x];
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

    bool auto_ready = false;
    if (game_state.is_active) {
        if (game_state.turn_duration_seconds < 10) auto_ready = true;
    }

    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd;

    // ----- Динамические Lua-команды -----
    // Освобождаем мьютекс, чтобы Lua не блокировал сервер
    lock.unlock();

    lua_getglobal(gLuaState, cmd.c_str());
    if (lua_isfunction(gLuaState, -1)) {
        // Проверяем, активна ли команда
        bool is_active = false;
        {
            lock_guard<mutex> lock(lua_commands_mutex);
            is_active = (active_lua_commands.find(cmd) != active_lua_commands.end());
        }
        if (!is_active) {
            string err = "[ERROR]|Lua command '" + cmd + "' is currently disabled by admin.\n";
            send(client_sock, err.c_str(), err.size(), 0);
            lua_pop(gLuaState, 1); // убираем функцию
            return;
        }
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
            if (auto_ready) {
                lock_guard<mutex> lock(game_mutex);
                auto it = game_state.players.find(player_id);
                if (it != game_state.players.end()) {
                    if (it->second.last_action == ActionType::WAIT) {
                        it->second.start_x = it->second.x;
                        it->second.start_y = it->second.y;
                    }
                    it->second.last_action = ActionType::LUA;
                    it->second.is_ready = true;
                }
            }
            else {
                // не авто-подтверждение – не делаем ready, но сохраняем стартовую позицию
                lock_guard<mutex> lock(game_mutex);
                auto it = game_state.players.find(player_id);
                if (it != game_state.players.end() && it->second.last_action == ActionType::WAIT) {
                    it->second.start_x = it->second.x;
                    it->second.start_y = it->second.y;
                    it->second.last_action = ActionType::LUA;
                }
            }
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
        {
            string direction;
            iss >> direction;
            if (game_state.players.find(player_id) == game_state.players.end()) {
                send(client_sock, "Player not found!\n", 19, 0);
                return;
            }
            auto& player = game_state.players[player_id];
            if (player.is_ready && player.last_action != ActionType::WAIT) {
                send(client_sock, "You already performed an action this turn.Use / unready to cancel it.\n", 69, 0);
                return;
            }
            if (!player.can_move) {
                send(client_sock, "You cannot move right now (can_move is false).\n", 51, 0);
                return;
            }


            int new_x = player.x, new_y = player.y;
            if (direction == "u") new_y++;
            else if (direction == "d") new_y--;
            else if (direction == "r") new_x++;
            else if (direction == "l") new_x--;
            else {
                send(client_sock, "Invalid direction. Use: u - up, d - down, l - left, r - right\n", 50, 0);
                return;
            }

            // Проверка проходимости (границы и walkable)
            if (!is_walkable(new_x, new_y)) {
                send(client_sock, "You cannot go there! The tile is blocked.\n", 43, 0);
                return;
            }

            int old_x = player.x, old_y = player.y;
            // Вызов событий выхода/входа (можно сделать до или после обновления координат,
            // но лучше до, чтобы old_x,old_y были старыми)
            on_player_enter_tile(player, old_x, old_y, new_x, new_y);

            if (player.last_action == ActionType::WAIT) {
                player.start_x = player.x;
                player.start_y = player.y;
            }

            // Обновляем координаты
            player.x = new_x;
            player.y = new_y;
            player.last_action = ActionType::MOVE;
            if (!auto_ready) {
                player.is_ready = false;  // чтобы игрок мог отменить через /unready
            }

            string response = "You moved to " + direction + ". Position: (" +
                to_string(player.x) + "," + to_string(player.y) + ")\n";
            send(client_sock, response.c_str(), static_cast<int>(response.length()), 0);
            string broadcast_msg = player.name + " moved to " + direction + ".";
            lock.unlock();
            broadcast_message(broadcast_msg, client_sock);
        }
    }
    else if (cmd == "skip") {
        if (game_state.players.find(player_id) == game_state.players.end()) {
            send(client_sock, "Player not found!\n", 19, 0);
            return;
        }
        auto& player = game_state.players[player_id];
        if (player.last_action == ActionType::WAIT) {
            player.start_x = player.x;
            player.start_y = player.y;
        }
        player.last_action = ActionType::SKIP;
        if (auto_ready) player.is_ready = true;
        else player.is_ready = false;
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
        if (player.is_ready) {
            send(client_sock, "You are already ready – cannot unready after confirming.\n", 60, 0);
            return;
        }
        // Восстанавливаем позицию (если было движение)
        player.x = player.start_x;
        player.y = player.start_y;
        player.last_action = ActionType::WAIT;   // разрешаем выбрать новое действие
        player.is_ready = false;
        send(client_sock, "Your action was cancelled. You are back at starting position.\n", 66, 0);
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
        string map_str = "[MAP]\n[c1][bg4] === Game Map === [/bg4][/c1]\n";
        for (int abs_y = 0; abs_y < map_height; ++abs_y) {
            for (int abs_x = 0; abs_x < map_width; ++abs_x) {
                int log_x = abs_x - map_center_x;
                int log_y = map_center_y - abs_y;
                bool has_player = false;
                for (auto& pair : game_state.players) {
                    if (pair.second.x == log_x && pair.second.y == log_y) {
                        map_str += to_string(pair.second.id);
                        has_player = true;
                        break;
                    }
                }
                if (!has_player) {
                    int tid = world_map[abs_y][abs_x];
                    char symbol = get_tile_char(tid);
                    map_str += symbol;
                }
                map_str += " ";
            }
            map_str += "\n";
        }
        map_str += "================\n";
        send(client_sock, map_str.c_str(), static_cast<int>(map_str.length()), 0);
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
            string broadcast_msg = "Turn duration set to " + to_string(seconds / 60) + " minutes and " + to_string(seconds - ((seconds / 60) * 60));
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
        else if (cmd == "save_tiles") {
            string filename;
            iss >> filename;
            if (filename.empty()) filename = "tiles.mcgtile";
            save_tiles(filename);
            send(client_sock, ("Tiles saved to " + filename + "\n").c_str(), 0, 0);
        }
        else if (cmd == "load_tiles") {
            string filename;
            iss >> filename;
            if (filename.empty()) filename = "tiles.mcgtile";
            load_tiles(filename);
            send(client_sock, ("Tiles loaded from " + filename + "\n").c_str(), 0, 0);
        }
        else if (cmd == "tile_set_display") {
            int tile_id;
            string display_str;
            iss >> tile_id;
            getline(iss, display_str);
            if (!display_str.empty() && display_str[0] == ' ') display_str.erase(0, 1);
            if (tile_types.find(tile_id) != tile_types.end()) {
                // заменяем \\n на реальные \n
                size_t pos = 0;
                while ((pos = display_str.find("\\n", pos)) != string::npos) {
                    display_str.replace(pos, 2, "\n");
                    pos += 1;
                }
                tile_types[tile_id].display = display_str;
                send(client_sock, ("Display for tile " + to_string(tile_id) + " set.\n").c_str(), 0, 0);
            }
            else {
                send(client_sock, "Tile ID not found. Use /tile_create first.\n", 42, 0);
            }
        }
        else if (cmd == "tile_set_walkable") {
            int tile_id, walkable;
            iss >> tile_id >> walkable;
            if (tile_types.find(tile_id) != tile_types.end()) {
                tile_types[tile_id].walkable = (walkable != 0);
                send(client_sock, ("Tile " + to_string(tile_id) + " walkable set to " + (walkable ? "true" : "false") + "\n").c_str(), 0, 0);
            }
            else {
                send(client_sock, "Tile ID not found.\n", 19, 0);
            }
        }
        else if (cmd == "tile_set_on_enter") {
            int tile_id;
            string func;
            iss >> tile_id >> func;
            if (tile_types.find(tile_id) != tile_types.end()) {
                tile_types[tile_id].on_enter = func;
                send(client_sock, ("Tile " + to_string(tile_id) + " on_enter = " + func + "\n").c_str(), 0, 0);
            }
            else {
                send(client_sock, "Tile ID not found.\n", 19, 0);
            }
        }
        else if (cmd == "tile_set_on_exit") {
            int tile_id;
            string func;
            iss >> tile_id >> func;
            if (tile_types.find(tile_id) != tile_types.end()) {
                tile_types[tile_id].on_exit = func;
                send(client_sock, ("Tile " + to_string(tile_id) + " on_exit = " + func + "\n").c_str(), 0, 0);
            }
            else {
                send(client_sock, "Tile ID not found.\n", 19, 0);
            }
        }
        else if (cmd == "tile_set_on_step") {
            int tile_id;
            string func;
            iss >> tile_id >> func;
            if (tile_types.find(tile_id) != tile_types.end()) {
                tile_types[tile_id].on_step = func;
                send(client_sock, ("Tile " + to_string(tile_id) + " on_step = " + func + "\n").c_str(), 0, 0);
            }
            else {
                send(client_sock, "Tile ID not found.\n", 19, 0);
            }
        }
        else if (cmd == "tile_info") {
            int tile_id;
            iss >> tile_id;
            auto it = tile_types.find(tile_id);
            if (it != tile_types.end()) {
                const Tile& t = it->second;
                string info = "=== Tile ID " + to_string(t.id) + " ===\n";
                info += "Walkable: " + string(t.walkable ? "yes" : "no") + "\n";
                info += "on_enter: " + (t.on_enter.empty() ? "(none)" : t.on_enter) + "\n";
                info += "on_exit: " + (t.on_exit.empty() ? "(none)" : t.on_exit) + "\n";
                info += "on_step: " + (t.on_step.empty() ? "(none)" : t.on_step) + "\n";
                info += "Display:\n" + t.display + "\n";
                send(client_sock, info.c_str(), (int)info.size(), 0);
            }
            else {
                send(client_sock, "Tile ID not found.\n", 19, 0);
            }
        }
        else if (cmd == "tile_create") {
            int tile_id;
            iss >> tile_id;
            if (tile_types.find(tile_id) == tile_types.end()) {
                Tile new_tile;
                new_tile.id = tile_id;
                new_tile.walkable = true;
                new_tile.display = "?";
                tile_types[tile_id] = new_tile;
                send(client_sock, ("Tile " + to_string(tile_id) + " created.\n").c_str(), 0, 0);
            }
            else {
                send(client_sock, "Tile already exists.\n", 21, 0);
            }
        }
        else if (cmd == "set_tile") {
            int x, y, new_id;
            if (!(iss >> x >> y >> new_id)) {
                send(client_sock, "Usage: /set_tile <x> <y> <new_id>\n", 35, 0);
                return;
            }
            int abs_x = map_center_x + x;
            int abs_y = map_center_y - y;
            if (abs_x < 0 || abs_x >= map_width || abs_y < 0 || abs_y >= map_height) {
                send(client_sock, "Coordinates out of bounds.\n", 28, 0);
                return;
            }
            set_tile(x, y, new_id); // но set_tile внутри снова проверит, можно убрать проверку внутри
            send(client_sock, ("Tile at (" + to_string(x) + "," + to_string(y) + ") set to id " + to_string(new_id) + "\n").c_str(), 0, 0);
        }
        else if (cmd == "get_tile") {
            int x, y;
            if (!(iss >> x >> y)) {
                send(client_sock, "Usage: /get_tile <x> <y>\n", 27, 0);
                return;
            }
            int abs_x = map_center_x + x;
            int abs_y = map_center_y - y;
            if (abs_x < 0 || abs_x >= map_width || abs_y < 0 || abs_y >= map_height) {
                send(client_sock, "Coordinates out of bounds.\n", 28, 0);
                return;
            }
            int tid = world_map[abs_y][abs_x];
            string info = "Tile at (" + to_string(x) + "," + to_string(y) + ") has id " + to_string(tid) + "\n";
            auto it = tile_types.find(tid);
            if (it != tile_types.end()) {
                info += "Walkable: " + string(it->second.walkable ? "yes" : "no") + "\n";
                info += "Display: " + it->second.display + "\n";
            }
            else {
                info += "(No properties defined for this tile id)\n";
            }
            send(client_sock, info.c_str(), info.size(), 0);
        }
        else if (cmd == "fix_players") {
            correct_all_players_positions();
            send(client_sock, "All players' positions have been validated and corrected if needed.\n", 70, 0);
        }
        else if (cmd == "save_map") {
            string filename;
            iss >> filename;
            if (filename.empty()) filename = "World.txt";
            save_world_map(filename);
            send(client_sock, ("World map saved to " + filename + "\n").c_str(), 0, 0);
        }
        else if (cmd == "reload_map") {
            load_world_map("World.txt");
            send(client_sock, "World map reloaded from World.txt\n", 35, 0);
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
            << player.is_admin << " "
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

        correct_all_players_positions();

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
            save_tiles("autosave_tiles.mcgtile");
            ConsoleHelper::SetColor(10);
            cout << "Auto-save completed (game + tiles)" << endl;
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
    unordered_set<string> new_available;
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
        new_available.insert(cmd_name);

        // Получение описания (как было раньше)...
        lua_getglobal(gLuaState, "get_description");
        if (lua_isfunction(gLuaState, -1)) {
            if (lua_pcall(gLuaState, 0, 1, 0) == LUA_OK) {
                const char* desc = lua_isstring(gLuaState, -1) ? lua_tostring(gLuaState, -1) : "";
                lock_guard<mutex> lock(lua_desc_mutex);
                lua_command_descriptions[cmd_name] = desc;
                lua_pop(gLuaState, 1);
            }
            else {
                cerr << "Error calling get_description() in " << filename << endl;
                lua_pop(gLuaState, 1);
                lock_guard<mutex> lock(lua_desc_mutex);
                lua_command_descriptions[cmd_name] = "";
            }
        }
        else {
            lua_pop(gLuaState, 1);
            lock_guard<mutex> lock(lua_desc_mutex);
            lua_command_descriptions[cmd_name] = "";
        }

        cout << "Loaded: " << filename << endl;
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    // Обновляем доступные команды
    {
        lock_guard<mutex> lock(lua_commands_mutex);
        available_lua_commands = move(new_available);
        // Удаляем из активных те команды, которых больше нет в available
        for (auto it = active_lua_commands.begin(); it != active_lua_commands.end(); ) {
            if (available_lua_commands.find(*it) == available_lua_commands.end())
                it = active_lua_commands.erase(it);
            else
                ++it;
        }
    }
}

bool save_lua_preset(const string& preset_name, string& error_msg) {
    // Создаём папку presets (если нет)
    if (CreateDirectoryA("presets", NULL) == 0) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            error_msg = "Failed to create directory 'presets' (error " + to_string(err) + ")";
            return false;
        }
    }

    string filename = "presets/" + preset_name + ".mcglua";
    ofstream file(filename);
    if (!file.is_open()) {
        error_msg = "Cannot open file: " + filename;
        return false;
    }

    {
        lock_guard<mutex> lock(lua_commands_mutex);
        if (active_lua_commands.empty()) {
            error_msg = "No active Lua commands to save.";
            return false;
        }
        for (const auto& cmd : active_lua_commands) {
            file << cmd << "\n";
            if (!file.good()) {
                error_msg = "Write error while saving";
                return false;
            }
        }
    }
    file.close();

    // Диагностика в консоль сервера
    ConsoleHelper::SetColor(10);
    cout << "[PRESET] Saved '" << preset_name << "' with " << active_lua_commands.size() << " commands." << endl;
    ConsoleHelper::SetColor(8);
    return true;
}

bool load_lua_preset(const string& preset_name, string& error_msg) {
    string filename = "presets/" + preset_name + ".mcglua";
    ifstream file(filename);
    if (!file.is_open()) {
        error_msg = "Preset file not found: " + filename;
        return false;
    }

    unordered_set<string> new_active;
    string line;
    while (getline(file, line)) {
        if (!line.empty()) new_active.insert(line);
    }
    file.close();

    {
        lock_guard<mutex> lock(lua_commands_mutex);
        active_lua_commands.clear();
        for (const auto& cmd : new_active) {
            if (available_lua_commands.count(cmd))
                active_lua_commands.insert(cmd);
            else
                cout << "[WARN] Preset contains unknown command: " << cmd << endl;
        }
    }

    ConsoleHelper::SetColor(10);
    cout << "[PRESET] Loaded '" << preset_name << "' -> " << active_lua_commands.size() << " active commands." << endl;
    ConsoleHelper::SetColor(8);
    return true;
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
                    // Поиск стартовой позиции
                    int start_x = 0, start_y = 0;
                    if (is_walkable(start_x, start_y)) {
                        player.x = start_x;
                        player.y = start_y;
                    }
                    else {
                        bool found = false;
                        for (int dy = -map_center_y; dy <= (map_height - 1 - map_center_y) && !found; ++dy) {
                            for (int dx = -map_center_x; dx <= (map_width - 1 - map_center_x) && !found; ++dx) {
                                if (is_walkable(dx, dy)) {
                                    start_x = dx; start_y = dy;
                                    found = true;
                                }
                            }
                        }
                        player.x = start_x;
                        player.y = start_y;
                    }
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
                        if (game_state.is_active == true && game_state.players.find(client_info[client_sock].second) != game_state.players.end()) {
                            help_msg += "==================--   Player's   --================\n";
                            help_msg += "/move [direction] - [c8]Move (u - up, d - down, l - left, r - right)[/c8]\n";
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
                    // Вместо перебора всех файлов .lua, используется available_lua_commands и active_lua_commands
                    if (!available_lua_commands.empty()) {
                        help_msg += "===--- Dynamic (Lua) Commands ---===\n";
                        lock_guard<mutex> lock(lua_commands_mutex);
                        for (const auto& cmd : available_lua_commands) {
                            if (active_lua_commands.count(cmd) == 0) continue; // не показываем неактивные
                            help_msg += "/" + cmd;
                            {
                                lock_guard<mutex> desc_lock(lua_desc_mutex);
                                auto it = lua_command_descriptions.find(cmd);
                                if (it != lua_command_descriptions.end() && !it->second.empty())
                                    help_msg += " - " + it->second;
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
                        help_msg += "/lua_list - Show all Lua commands and their status\n";
                        help_msg += "/lua_enable <cmd> - Activate a Lua command\n";
                        help_msg += "/lua_disable <cmd> - Deactivate a Lua command\n";
                        help_msg += "/lua_preset_save <name> - Save current active set as preset\n";
                        help_msg += "/lua_preset_load <name> - Load a preset (replaces active set)\n";
                        help_msg += "/lua_preset_list - List all saved presets\n";
                        help_msg += "------------  Map commands  -------------\n";
                        help_msg += "/save_tiles [file] - Save tile properties\n";
                        help_msg += "/load_tiles [file] - Load tile properties\n";
                        help_msg += "/tile_create <id> - Create new tile type\n";
                        help_msg += "/tile_set_display <id> <text with \\n> - Set tile appearance (use \\n for newline)\n";
                        help_msg += "/tile_set_walkable <id> <0/1> - Set walkable flag (0 - false, 1 - true)\n";
                        help_msg += "/tile_set_on_enter <id> <lua_func> - Set on_enter handler\n";
                        help_msg += "/tile_set_on_exit <id> <lua_func> - Set on_exit handler\n";
                        help_msg += "/tile_set_on_step <id> <lua_func> - Set on_step handler (called each turn if player didn't move)\n";
                        help_msg += "/tile_info <id> - Show tile details\n";
                        help_msg += "/fix_players - Check and fix all players positions (to nearest walkable tile)\n";
                        help_msg += "/set_tile <x> <y> <new_id> - Change tile at logical coordinates\n";
                        help_msg += "/get_tile <x> <y> - Show tile info at logical coordinates\n";
                        help_msg += "/save_map [filename] - Save current map to file (default: World.txt)\n";
                        help_msg += "/reload_map - Reload World.txt map\n";
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
                        broadcast_message("=== GAME STARTED ===\nTurn duration: " + to_string(game_state.turn_duration_seconds / 60) + " minutes and " + to_string(game_state.turn_duration_seconds - ((game_state.turn_duration_seconds / 60) * 60)) + " seconds\n", INVALID_SOCKET);
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
                    else if (message == "/lua_list") {
                        lock_guard<mutex> lock(lua_commands_mutex);
                        string msg = "\n=== Lua Commands Status ===\n";
                        for (const auto& cmd : available_lua_commands) {
                            msg += (active_lua_commands.count(cmd) ? "[ACTIVE] " : "[INACTIVE] ");
                            msg += cmd;
                            // Добавляем описание, если есть
                            lock_guard<mutex> desc_lock(lua_desc_mutex);
                            auto it = lua_command_descriptions.find(cmd);
                            if (it != lua_command_descriptions.end() && !it->second.empty())
                                msg += " - " + it->second;
                            msg += "\n";
                        }
                        msg += "==========================\n";
                        send(client_sock, msg.c_str(), msg.size(), 0);
                        continue;
                    }
                    else if (message.substr(0, 11) == "/lua_enable") {
                        if (message != "/lua_enable") {
                            string cmd = message.substr(12);
                            if (cmd.empty()) {
                                send(client_sock, "[ERROR] Usage: /lua_enable <command_name>\n", 42, 0);
                                continue;
                            }
                            bool success = false;
                            {
                                lock_guard<mutex> lock(lua_commands_mutex);
                                if (available_lua_commands.count(cmd)) {
                                    active_lua_commands.insert(cmd);
                                    success = true;
                                }
                            }
                            if (success) {
                                string ok = "Lua command '" + cmd + "' is now ACTIVE.\n";
                                send(client_sock, ok.c_str(), ok.size(), 0);
                            }
                            else {
                                string err = "[ERROR] Lua command '" + cmd + "' not found.\n";
                                send(client_sock, err.c_str(), err.size(), 0);
                            }
                            continue;
                        }
                    }
                    else if (message.substr(0, 12) == "/lua_disable") {
                        if (message != "/lua_disable") {
                            string cmd = message.substr(13);
                            if (cmd.empty()) {
                                send(client_sock, "[ERROR] Usage: /lua_disable <command_name>\n", 43, 0);
                                continue;
                            }
                            bool removed = false;
                            {
                                lock_guard<mutex> lock(lua_commands_mutex);
                                removed = (active_lua_commands.erase(cmd) > 0);
                            }
                            if (removed) {
                                string ok = "Lua command '" + cmd + "' is now INACTIVE.\n";
                                send(client_sock, ok.c_str(), ok.size(), 0);
                            }
                            else {
                                string err = "[ERROR] Lua command '" + cmd + "' is not active.\n";
                                send(client_sock, err.c_str(), err.size(), 0);
                            }
                            continue;
                        }
                    }
                    else if (message.substr(0, 16) == "/lua_preset_save") {
                        string preset = message.substr(17);
                        if (preset.empty()) {
                            send(client_sock, "[ERROR] Usage: /lua_preset_save <preset_name>\n", 49, 0);
                            continue;
                        }
                        if (preset.find_first_of("\\/:*?\"<>|") != string::npos) {
                            send(client_sock, "[ERROR] Invalid preset name (cannot contain \\ / : * ? \" < > |)\n", 70, 0);
                            continue;
                        }
                        string error_msg;
                        if (save_lua_preset(preset, error_msg)) {
                            string ok = "[OK] Preset '" + preset + "' saved.\n";
                            send(client_sock, ok.c_str(), (int)ok.size(), 0);
                        }
                        else {
                            string err = "[ERROR] " + error_msg + "\n";
                            send(client_sock, err.c_str(), (int)err.size(), 0);
                        }
                        continue;
                    }
                    else if (message.substr(0, 16) == "/lua_preset_load") {
                        string preset = message.substr(17);
                        if (preset.empty()) {
                            send(client_sock, "[ERROR] Usage: /lua_preset_load <preset_name>\n", 49, 0);
                            continue;
                        }
                        if (preset.find_first_of("\\/:*?\"<>|") != string::npos) {
                            send(client_sock, "[ERROR] Invalid preset name.\n", 29, 0);
                            continue;
                        }
                        string error_msg;
                        if (load_lua_preset(preset, error_msg)) {
                            string ok = "[OK] Preset '" + preset + "' loaded. Use /lua_list to see active commands.\n";
                            send(client_sock, ok.c_str(), (int)ok.size(), 0);
                        }
                        else {
                            string err = "[ERROR] " + error_msg + "\n";
                            send(client_sock, err.c_str(), (int)err.size(), 0);
                        }
                        continue;
                    }
                    else if (message == "/lua_preset_list") {
                        CreateDirectoryA("presets", NULL);
                        WIN32_FIND_DATAA findData;
                        HANDLE hFind = FindFirstFileA("presets/*.mcglua", &findData);
                        if (hFind == INVALID_HANDLE_VALUE) {
                            send(client_sock, "No presets found.\n", 18, 0);
                            continue;
                        }
                        string msg = "\n=== Available Presets ===\n";
                        do {
                            string fname = findData.cFileName;
                            size_t dot = fname.find_last_of('.');
                            if (dot != string::npos) fname = fname.substr(0, dot);
                            msg += fname + "\n";
                        } while (FindNextFileA(hFind, &findData));
                        FindClose(hFind);
                        msg += "=========================\n";
                        send(client_sock, msg.c_str(), (int)msg.size(), 0);
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

    // Загружаем карту
    load_world_map("Default_World.txt");
    // Пытаемся загрузить ранее сохранённые настройки тайлов (если есть)
    load_tiles("autosave_tiles.mcgtile");

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