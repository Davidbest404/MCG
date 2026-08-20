#include "ServerCommands.h"
#include "ServerShared.h"
#include "ServerMap.h"
#include "ServerGame.h"
#include "ServerLua.h"
#include "../Common/ConsoleHelper.h"
#include <sstream>
#include <iostream>
#include <thread>
#include <fstream>      // <-- ДОБАВЛЕНО для ifstream/ofstream
#include <algorithm>    // <-- ДОБАВЛЕНО для transform
#include <string>

using namespace std;

// Внешние зависимости
extern GameState game_state;
extern mutex game_mutex;
extern mutex default_attrs_mutex;
extern unordered_map<string, variant<int, float, string, bool>> default_attrs;
extern unordered_set<string> available_lua_commands;
extern unordered_set<string> active_lua_commands;
extern mutex lua_commands_mutex;
extern map<string, string> lua_command_descriptions;
extern mutex lua_desc_mutex;
extern string server_description, server_rules;
extern mutex server_info_mutex;
extern lua_State* gLuaState;
extern map<int, Tile> tile_types;

// ----- Реализации команд -----

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
    lock.unlock();

    lua_getglobal(gLuaState, cmd.c_str());
    if (lua_isfunction(gLuaState, -1)) {
        bool is_active = false;
        {
            lock_guard<mutex> lock(lua_commands_mutex);
            is_active = (active_lua_commands.find(cmd) != active_lua_commands.end());
        }
        if (!is_active) {
            string err = "[ERROR]|Lua command '" + cmd + "' is currently disabled by admin.\n";
            send(client_sock, err.c_str(), err.size(), 0);
            lua_pop(gLuaState, 1);
            return;
        }
        lua_pushinteger(gLuaState, player_id);
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
                string colored = string("[LUA] ") + result;
                send(client_sock, colored.c_str(), colored.size(), 0);
                send(client_sock, "\n", 1, 0);
            }
            lua_pop(gLuaState, 1);
        }
        return;
    }
    else {
        lua_pop(gLuaState, 1);
    }

    lock.lock();

    // ----- Встроенные команды -----
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
                send(client_sock, "You already performed an action this turn.Use /unready to cancel it.\n", 69, 0);
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

            if (!is_walkable(new_x, new_y)) {
                send(client_sock, "You cannot go there! The tile is blocked.\n", 43, 0);
                return;
            }

            int old_x = player.x, old_y = player.y;
            on_player_enter_tile(player, old_x, old_y, new_x, new_y);

            if (player.last_action == ActionType::WAIT) {
                player.start_x = player.x;
                player.start_y = player.y;
            }

            player.x = new_x;
            player.y = new_y;
            player.last_action = ActionType::MOVE;
            if (!auto_ready) {
                player.is_ready = false;
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
        player.x = player.start_x;
        player.y = player.start_y;
        player.last_action = ActionType::WAIT;
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
        if (world_tiles.empty()) {
            map_str += "(Map is empty)\n";
        }
        else {
            int min_x = INT_MAX, max_x = INT_MIN, min_y = INT_MAX, max_y = INT_MIN;
            for (const auto& [key, tid] : world_tiles) {
                int x = static_cast<int>(key >> 32);
                int y = static_cast<int>(key & 0xFFFFFFFF);
                min_x = min(min_x, x); max_x = max(max_x, x);
                min_y = min(min_y, y); max_y = max(max_y, y);
            }
            min_x--; max_x++; min_y--; max_y++;
            for (int y = max_y; y >= min_y; --y) {
                for (int x = min_x; x <= max_x; ++x) {
                    bool has_player = false;
                    for (auto& pair : game_state.players) {
                        if (pair.second.x == x && pair.second.y == y) {
                            map_str += to_string(pair.second.id);
                            has_player = true;
                            break;
                        }
                    }
                    if (!has_player) {
                        int tid = get_tile_id(x, y);
                        if (tid == 0) map_str += '.';
                        else {
                            char symbol = get_tile_char(tid);
                            map_str += symbol;
                        }
                    }
                    map_str += " ";
                }
                map_str += "\n";
            }
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
            LoadLuaScripts(gLuaState);
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
            set_tile(x, y, new_id);
            send(client_sock, ("Tile at (" + to_string(x) + "," + to_string(y) + ") set to id " + to_string(new_id) + "\n").c_str(), 0, 0);
        }
        else if (cmd == "get_tile") {
            int x, y;
            if (!(iss >> x >> y)) {
                send(client_sock, "Usage: /get_tile <x> <y>\n", 27, 0);
                return;
            }
            int tid = get_tile_id(x, y);
            string info = "Tile at (" + to_string(x) + "," + to_string(y) + ") has id " + to_string(tid) + "\n";
            if (tid != 0) {
                auto it = tile_types.find(tid);
                if (it != tile_types.end()) {
                    info += "Walkable: " + string(it->second.walkable ? "yes" : "no") + "\n";
                    info += "Display: " + it->second.display + "\n";
                }
                else {
                    info += "(No properties defined for this tile id)\n";
                }
            }
            else {
                info += "(Tile is empty/air)\n";
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
            if (filename.empty()) filename = "world.txt";
            save_world_map(filename);
            send(client_sock, ("World map saved to " + filename + "\n").c_str(), 0, 0);
        }
        else if (cmd == "reload_map") {
            load_world_map("world.txt");
            send(client_sock, "World map reloaded from world.txt\n", 35, 0);
        }
        else {
            send(client_sock, "Unknown admin command.\n", 24, 0);
        }
    }
    else {
        send(client_sock, "Unknown command. Type /help for available commands.\n", 55, 0);
    }
}

// ----- Обработчики атрибутов -----

void set_attr_for_all(const string& attr_name, const variant<int, float, string, bool>& value) {
    lock_guard<mutex> lock(game_mutex);
    for (auto& [id, player] : game_state.players) {
        player.setAttr(attr_name, value);
    }
}

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

    return value_str;
}

void handle_set_attr_all(SOCKET client_sock, const string& command) {
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd;
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

void handle_set_attr(SOCKET client_sock, const string& command, int caller_id, bool is_admin) {
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd;
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

void handle_get_attr(SOCKET client_sock, const string& command) {
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd;
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

void handle_has_attr(SOCKET client_sock, const string& command) {
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd;
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

void handle_remove_attr(SOCKET client_sock, const string& command, int caller_id, bool is_admin) {
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd;
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

void handle_remove_attr_all(SOCKET client_sock, const string& command) {
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd;
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
    iss >> cmd;
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
    iss >> cmd;
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

void handle_sync_default_attrs(SOCKET client_sock) {
    lock_guard<mutex> lock_def(default_attrs_mutex);
    lock_guard<mutex> lock_game(game_mutex);

    if (default_attrs.empty()) {
        string msg = "No default attributes set. Nothing to sync.\n";
        send(client_sock, msg.c_str(), (int)msg.size(), 0);
        return;
    }

    int updated_count = 0;
    for (auto& [id, player] : game_state.players) {
        for (const auto& [key, val] : default_attrs) {
            player.setAttr(key, val);
        }
        updated_count++;
    }

    string msg = "Default attributes synchronized to " + to_string(updated_count) + " existing players.\n";
    send(client_sock, msg.c_str(), (int)msg.size(), 0);
}

void handle_list_attrs(SOCKET client_sock, const string& command, int caller_id) {
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd;
    int target_id = caller_id;
    if (!(iss >> target_id)) {
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

void handle_edit_description(SOCKET client_sock, const string& command) {
    string text = command.substr(17);
    if (text.empty()) {
        string err = "[ERROR] Usage: /edit_description <text with \\n for newline>\n";
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
        server_description = text;
    }
    send(client_sock, "Server description updated.\n", 28, 0);
}

void handle_edit_rules(SOCKET client_sock, const string& command) {
    string text = command.substr(12);
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

bool save_lua_preset(const string& preset_name, string& error_msg) {
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