#include "ServerMap.h"
#include "ServerShared.h"
#include "../Common/ConsoleHelper.h"
#include <fstream>
#include <sstream>
#include <iostream>

using namespace std;

// ќпредел€ем глобальные переменные карты (они объ€влены extern в ServerShared.h)
unordered_map<long long, int> world_tiles;
map<int, Tile> tile_types;

// ¬нешние зависимости (определены в других модул€х)
extern mutex game_mutex;
extern GameState game_state;
extern lua_State* gLuaState;

// ----- –еализации -----

int get_tile_id(int x, int y) {
    auto it = world_tiles.find(make_tile_key(x, y));
    return (it != world_tiles.end()) ? it->second : 0;
}

bool is_walkable(int x, int y) {
    int tid = get_tile_id(x, y);
    if (tid == 0) return true;
    auto it = tile_types.find(tid);
    if (it == tile_types.end()) return true;
    return it->second.walkable;
}

void set_tile(int x, int y, int new_id) {
    if (new_id == 0) {
        remove_tile(x, y);
        return;
    }
    if (tile_types.find(new_id) == tile_types.end()) {
        Tile new_tile;
        new_tile.id = new_id;
        new_tile.walkable = true;
        new_tile.display = "?";
        tile_types[new_id] = new_tile;
        cout << "Auto-created tile id " << new_id << endl;
    }
    world_tiles[make_tile_key(x, y)] = new_id;
    correct_all_players_positions();
}

void remove_tile(int x, int y) {
    world_tiles.erase(make_tile_key(x, y));
    correct_all_players_positions();
}

void load_world_map(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Failed to load world map from " << filename << ". Starting with empty map." << endl;
        world_tiles.clear();
        return;
    }
    world_tiles.clear();
    string line;
    while (getline(file, line)) {
        if (line.empty()) continue;
        stringstream ss(line);
        string x_str, y_str, id_str;
        if (!getline(ss, x_str, ';')) continue;
        if (!getline(ss, y_str, ';')) continue;
        if (!getline(ss, id_str)) continue;
        try {
            int x = stoi(x_str);
            int y = stoi(y_str);
            int id = stoi(id_str);
            set_tile(x, y, id);
        }
        catch (...) {
            cerr << "Skipping invalid line in map file: " << line << endl;
        }
    }
    file.close();
    ConsoleHelper::SetColor(10);
    cout << "World map loaded from " << filename << ". Tiles count: " << world_tiles.size() << endl;
    ConsoleHelper::SetColor(8);
}

void save_world_map(const string& filename) {
    ofstream file(filename);
    if (!file) {
        cerr << "Failed to save world map to " << filename << endl;
        return;
    }
    for (const auto& [key, tid] : world_tiles) {
        int x = static_cast<int>(key >> 32);
        int y = static_cast<int>(key & 0xFFFFFFFF);
        file << x << ";" << y << ";" << tid << "\n";
    }
    file.close();
    ConsoleHelper::SetColor(10);
    cout << "World map saved to " << filename << ". Tiles saved: " << world_tiles.size() << endl;
    ConsoleHelper::SetColor(8);
}

void save_tiles(const string& filename) {
    ofstream file(filename);
    if (!file) {
        cerr << "Failed to save tiles to " << filename << endl;
        return;
    }
    file << tile_types.size() << endl;
    for (const auto& [id, tile] : tile_types) {
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
    return disp[0];
}

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

void on_player_enter_tile(Player& player, int old_x, int old_y, int new_x, int new_y) {
    int old_tile_id = get_tile_id(old_x, old_y);
    if (old_tile_id != 0) {
        auto it = tile_types.find(old_tile_id);
        if (it != tile_types.end() && !it->second.on_exit.empty()) {
            call_tile_function(it->second.on_exit, player.id, old_x, old_y, old_tile_id);
        }
    }
    int new_tile_id = get_tile_id(new_x, new_y);
    if (new_tile_id != 0) {
        auto it = tile_types.find(new_tile_id);
        if (it != tile_types.end() && !it->second.on_enter.empty()) {
            call_tile_function(it->second.on_enter, player.id, new_x, new_y, new_tile_id);
        }
    }
}

bool find_nearest_walkable(int tx, int ty, int& out_x, int& out_y) {
    int radius = 100;
    int best_dist = INT_MAX;
    bool found = false;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int nx = tx + dx, ny = ty + dy;
            if (is_walkable(nx, ny)) {
                int dist = abs(nx - tx) + abs(ny - ty);
                if (dist < best_dist) {
                    best_dist = dist;
                    out_x = nx; out_y = ny;
                    found = true;
                }
            }
        }
    }
    return found;
}

bool correct_player_position(Player& player) {
    int old_x = player.x, old_y = player.y;
    if (is_walkable(old_x, old_y)) {
        return false;
    }
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

void correct_all_players_positions() {
    lock_guard<mutex> lock(game_mutex);
    for (auto& [id, player] : game_state.players) {
        correct_player_position(player);
    }
}