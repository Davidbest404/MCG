#include "ServerCommands.h"
#include "ServerShared.h"
#include "ServerMap.h"
#include "ServerGame.h"
#include "ServerLua.h"
#include "../Common/ConsoleHelper.h"
#include <sstream>
#include <iostream>
#include <thread>
#include <fstream>
#include <algorithm>
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
extern vector<SOCKET> clients;
extern atomic<int> client_count;
extern map<SOCKET, pair<string, int>> client_info;
extern map<SOCKET, bool> admin_clients;
extern string Name;
extern int PORT;
extern int max_clients;
extern string Password;

// ----- Вспомогательная функция для формирования помощи -----
string build_help_message(int player_id, bool is_admin) {
    string help_msg = "\n[COMMAND]\n";
    help_msg += "\n[c2][bgC]=== Available Commands ===[/bgC][/c2]\n";
    help_msg += "/help - Show this message\n";
    help_msg += "/list - List all connected clients\n";
    help_msg += "/description - Show server description\n";
    help_msg += "/rules - Show server rules\n";

    // Игровые команды (доступны, если игра активна)
    if (game_state.is_active && game_state.players.find(player_id) != game_state.players.end()) {
        help_msg += "==================--   Player's   --================\n";
        help_msg += "/move [direction] - [c8]Move (u - up, d - down, l - left, r - right)[/c8]\n";
        help_msg += "/skip - [c8]Skip your turn[/c8]\n";
        help_msg += "/status - [c8]Check your status[/c8]\n";
        help_msg += "/map - [c8]Show game map[/c8]\n";
        help_msg += "/ready - [c8]Mark yourself as ready[/c8]\n";
        help_msg += "/unready - [c8]Mark yourself as not ready[/c8]\n";
        help_msg += "/time_remaining - [c8]Check how much time left[/c8]\n";
        help_msg += "/set_my_view_radius <radius> - [c8]Set your view radius (1-20)[/c8]\n";
    }

    // Динамические Lua-команды (активные)
    if (!active_lua_commands.empty()) {
        help_msg += "===--- Dynamic (Lua) Commands ---===\n";
        lock_guard<mutex> lock(lua_commands_mutex);
        for (const auto& cmd : active_lua_commands) {
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

    // Админские команды
    if (is_admin) {
        help_msg += "=====--  Admin's  --=====\n";
        help_msg += "/kick <id> - Kick a client\n";
        help_msg += "/name <new_name> - Change server name\n";
        help_msg += "/max <number> - Change max clients\n";
        help_msg += "/info - Show server info\n";
        help_msg += "/clients - Show detailed client info (including sockets)\n";
        help_msg += "/set_can_move <player_id> <true/false> - Allow or forbid movement for a player\n";
        help_msg += "/save [filename] - Save game state (default: game_save.mcgsave)\n";
        help_msg += "/load [filename] - Load game state (default: game_save.mcgsave)\n";
        help_msg += "/edit_description <text with \\n> - Set server description (use \\n for newline)\n";
        help_msg += "/edit_rules <text with \\n> - Set server rules (use \\n for newline)\n";
        help_msg += "/start_game - Start the game\n";
        help_msg += "/pause_game - Pause the game\n";
        help_msg += "/set_turn_time <seconds> - Set turn duration\n";
        help_msg += "/end_turn - Force end current turn\n";
        help_msg += "/reload_scripts - Reload Lua actions\n";
        help_msg += "/set_hp <target_id> <hp> - Set HP (capped at max_hp)\n";
        help_msg += "/set_view_radius <player_id> <radius> - Set view radius for a player (1-20)\n";
        help_msg += "------------  Attributes  -------------\n";
        help_msg += "/set_attr_all <attr_name> <value> - Set attribute for ALL current players\n";
        help_msg += "/set_attr <target_id> <attr_name> <value> - Set attribute for specific player\n";
        help_msg += "/get_attr <target_id> <attr_name> - Get attribute value\n";
        help_msg += "/has_attr <target_id> <attr_name> - Check if attribute exists\n";
        help_msg += "/remove_attr <target_id> <attr_name> - Remove attribute from specific player\n";
        help_msg += "/remove_attr_all <attr_name> - Remove attribute from ALL players\n";
        help_msg += "/default_attr_add <attr_name> <value> - Add/modify default attribute for future players\n";
        help_msg += "/default_attr_remove <attr_name> - Remove default attribute\n";
        help_msg += "/default_attr_list - List all default attributes\n";
        help_msg += "/sync_default_attrs - Apply current default attributes to all existing players\n";
        help_msg += "/list_attrs [target_id] - List all attributes of a player (default: yourself)\n";
        help_msg += "------------  Map (tiles)  -------------\n";
        help_msg += "/save_tiles [file] - Save tile properties (default: tiles.mcgtile)\n";
        help_msg += "/load_tiles [file] - Load tile properties\n";
        help_msg += "/tile_create <id> - Create new tile type\n";
        help_msg += "/tile_set_display <id> <text with \\n> - Set tile appearance (use \\n for newline)\n";
        help_msg += "/tile_set_walkable <id> <0/1> - Set walkable flag (0 - false, 1 - true)\n";
        help_msg += "/tile_set_on_enter <id> <lua_func> - Set on_enter handler\n";
        help_msg += "/tile_set_on_exit <id> <lua_func> - Set on_exit handler\n";
        help_msg += "/tile_set_on_step <id> <lua_func> - Set on_step handler\n";
        help_msg += "/tile_info <id> - Show tile details\n";
        help_msg += "/set_tile <x> <y> <new_id> - Change tile at logical coordinates\n";
        help_msg += "/get_tile <x> <y> - Show tile info at logical coordinates\n";
        help_msg += "/fix_players - Check and fix all players positions (to nearest walkable tile)\n";
        help_msg += "/save_map [filename] - Save current map (default: world.txt)\n";
        help_msg += "/reload_map - Reload map from world.txt\n";
        help_msg += "------------  Lua management  -------------\n";
        help_msg += "/lua_list - Show all Lua commands and their status\n";
        help_msg += "/lua_enable <cmd> - Activate a Lua command\n";
        help_msg += "/lua_disable <cmd> - Deactivate a Lua command\n";
        help_msg += "/lua_activate_all - Activate ALL Lua commands\n";
        help_msg += "/lua_preset_save <name> - Save current active set as preset\n";
        help_msg += "/lua_preset_load <name> - Load a preset (replaces active set)\n";
        help_msg += "/lua_preset_list - List all saved presets\n";
    }
    help_msg += "====-----          -----====\n";
    return help_msg;
}

// ----- Реализация команд -----
void process_game_command(SOCKET client_sock, const string& command, int player_id, bool is_admin) {
    if (!game_mutex.try_lock()) {
        send(client_sock, "Server is busy processing other commands. Please try again.\n", 68, 0);
        return;
    }
    unique_lock<mutex> lock(game_mutex, adopt_lock);  // блокировка удерживается до конца функции

    // Разрешаем определённые команды даже если игра не активна
    bool is_allowed_always = (command == "/help" || command == "/list" ||
        command == "/description" || command == "/rules" ||
        command == "/status" || command == "/map" ||
        command == "/set_my_view_radius" ||
        command.find("/set_") == 0);

    if (!game_state.is_active && !is_allowed_always && command != "/start_game") {
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
    // Освобождаем блокировку, чтобы Lua мог вызывать функции, которые тоже могут захватывать game_mutex
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
            // Возвращаем блокировку перед выходом
            lock.lock();
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
        // Восстанавливаем блокировку
        lock.lock();
        return;
    }
    else {
        lua_pop(gLuaState, 1);
    }

    // Восстанавливаем блокировку
    lock.lock();

    // ----- Встроенные команды -----
    if (cmd == "help") {
        string help_msg = build_help_message(player_id, is_admin);
        send(client_sock, help_msg.c_str(), static_cast<int>(help_msg.length()), 0);
    }
    else if (cmd == "list") {
        string list_msg = "\n=== Connected Clients (" + to_string(client_count) + ") ===\n";
        for (auto& client : clients) {
            if (client_info.find(client) != client_info.end()) {
                auto& info = client_info[client];
                list_msg += "ID: " + to_string(info.second) + " | Name: " + info.first;
                if (admin_clients[client]) list_msg += " [ADMIN]";
                {
                    // game_mutex уже захвачен, безопасно обращаемся к game_state
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
    }
    else if (cmd == "description") {
        handle_description(client_sock);
    }
    else if (cmd == "rules") {
        handle_rules(client_sock);
    }
    else if (cmd == "time_remaining") {
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
            // Освобождаем блокировку перед broadcast, чтобы избежать дедлока
            lock.unlock();
            broadcast_message(broadcast_msg, client_sock);
            lock.lock();
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
            lock.lock();
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
        status += "View radius: " + to_string(player.view_radius) + "\n";
        status += "==================\n";
        send(client_sock, status.c_str(), static_cast<int>(status.length()), 0);
    }
    else if (cmd == "map") {
        auto it_player = game_state.players.find(player_id);
        if (it_player == game_state.players.end()) {
            send(client_sock, "Player not found!\n", 19, 0);
            return;
        }
        Player& player = it_player->second;
        int radius = player.view_radius;
        int min_x = player.x - radius;
        int max_x = player.x + radius;
        int min_y = player.y - radius;
        int max_y = player.y + radius;

        stringstream data;
        data << "MAP_DATA|" << player_id << ";" << player.x << ";" << player.y << ";" << radius << "|";

        for (auto& pair : game_state.players) {
            int dx = pair.second.x - player.x;
            int dy = pair.second.y - player.y;
            if (abs(dx) <= radius && abs(dy) <= radius) {
                data << pair.second.id << ";" << pair.second.x << ";" << pair.second.y << ";P|";
            }
        }

        for (const auto& [key, tid] : world_tiles) {
            int x = static_cast<int>(key >> 32);
            int y = static_cast<int>(key & 0xFFFFFFFF);
            int dx = x - player.x;
            int dy = y - player.y;
            if (abs(dx) <= radius && abs(dy) <= radius) {
                char symbol = get_tile_char(tid);
                data << tid << ";" << x << ";" << y << ";T:" << symbol << "|";
            }
        }

        string map_data = data.str();
        if (map_data.back() == '|')
            map_data.pop_back();
        map_data += "\n";

        string full_msg = "[MAP]\n" + map_data;
        send(client_sock, full_msg.c_str(), static_cast<int>(full_msg.length()), 0);
    }
    else if (cmd == "set_my_view_radius") {
        int new_radius;
        if (!(iss >> new_radius)) {
            send(client_sock, "Usage: /set_my_view_radius <radius>\n", 37, 0);
            return;
        }
        auto it = game_state.players.find(player_id);
        if (it == game_state.players.end()) {
            send(client_sock, "Player not found.\n", 19, 0);
            return;
        }
        if (new_radius < 1 || new_radius > 20) {
            send(client_sock, "Radius must be between 1 and 20.\n", 33, 0);
            return;
        }
        it->second.view_radius = new_radius;
        string msg = "Your view radius set to " + to_string(new_radius) + "\n";
        send(client_sock, msg.c_str(), msg.size(), 0);
    }
    else if (is_admin) {
        // ----- Админские команды -----
        if (cmd == "kick") {
            int kick_id;
            if (!(iss >> kick_id)) {
                send(client_sock, "Usage: /kick <player_id>\n", 26, 0);
                return;
            }
            bool found = false;
            for (auto& client : clients) {
                if (client_info.find(client) != client_info.end() && client_info[client].second == kick_id) {
                    if (!admin_clients[client]) {
                        string kick_msg = "[SERVER] You have been kicked by admin";
                        send(client, kick_msg.c_str(), static_cast<int>(kick_msg.length()), 0);
                        closesocket(client);
                        string notify = "[SERVER] Client ID " + to_string(kick_id) + " was kicked by admin";
                        lock.unlock();
                        broadcast_message(notify, INVALID_SOCKET);
                        lock.lock();
                        found = true;
                        break;
                    }
                    else {
                        send(client_sock, "Cannot kick another admin.\n", 28, 0);
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                send(client_sock, "Client not found.\n", 19, 0);
            }
        }
        else if (cmd == "name") {
            string new_name;
            if (!(iss >> new_name)) {
                send(client_sock, "Usage: /name <new_name>\n", 25, 0);
                return;
            }
            Name = new_name;
            string notify = "[SERVER] Server name changed to: " + Name;
            lock.unlock();
            broadcast_message(notify, INVALID_SOCKET);
            lock.lock();
        }
        else if (cmd == "max") {
            int new_max;
            if (!(iss >> new_max)) {
                send(client_sock, "Usage: /max <number>\n", 22, 0);
                return;
            }
            if (new_max > 0 && new_max < 1000) {
                max_clients = new_max;
                string notify = "[SERVER] Max clients changed to: " + to_string(max_clients);
                lock.unlock();
                broadcast_message(notify, INVALID_SOCKET);
                lock.lock();
            }
            else {
                send(client_sock, "Max clients must be between 1 and 1000.\n", 40, 0);
            }
        }
        else if (cmd == "info") {
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
        }
        else if (cmd == "clients") {
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
        }
        else if (cmd == "set_can_move") {
            int target_id;
            string val;
            if (!(iss >> target_id >> val)) {
                send(client_sock, "Usage: /set_can_move <player_id> <true/false>\n", 48, 0);
                return;
            }
            bool can_move;
            if (val == "true") can_move = true;
            else if (val == "false") can_move = false;
            else {
                send(client_sock, "Value must be true or false\n", 29, 0);
                return;
            }
            auto it = game_state.players.find(target_id);
            if (it != game_state.players.end()) {
                it->second.can_move = can_move;
                string msg = "Player " + to_string(target_id) + " can_move set to " + (can_move ? "true" : "false") + "\n";
                send(client_sock, msg.c_str(), msg.size(), 0);
            }
            else {
                send(client_sock, "Player not found\n", 18, 0);
            }
        }
        else if (cmd == "save") {
            string filename;
            if (!(iss >> filename)) filename = "game_save.mcgsave";
            // Запускаем в отдельном потоке, чтобы не держать блокировку
            thread(save_game_state, filename).detach();
            send(client_sock, "Saving game...\n", 15, 0);
        }
        else if (cmd == "load") {
            string filename;
            if (!(iss >> filename)) filename = "game_save.mcgsave";
            // Запускаем в отдельном потоке
            thread(load_game_state, filename).detach();
            send(client_sock, "Loading game...\n", 16, 0);
        }
        else if (cmd == "start_game") {
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
            lock.lock();
        }
        else if (cmd == "pause_game") {
            game_state.is_active = false;
            lock.unlock();
            broadcast_message("Game paused by admin.", INVALID_SOCKET);
            lock.lock();
        }
        else if (cmd == "set_turn_time") {
            int seconds;
            if (!(iss >> seconds)) {
                send(client_sock, "Usage: /set_turn_time <seconds>\n", 33, 0);
                return;
            }
            game_state.turn_duration_seconds = seconds;
            string broadcast_msg = "Turn duration set to " + to_string(seconds / 60) + " minutes and " + to_string(seconds - ((seconds / 60) * 60));
            lock.unlock();
            broadcast_message(broadcast_msg, INVALID_SOCKET);
            lock.lock();
        }
        else if (cmd == "end_turn") {
            lock.unlock();
            process_turn_end();
            lock.lock();
        }
        else if (cmd == "set_hp") {
            int target_id, hp;
            if (!(iss >> target_id >> hp)) {
                send(client_sock, "Usage: /set_hp <target_id> <hp>\n", 33, 0);
                return;
            }
            auto it = game_state.players.find(target_id);
            if (it != game_state.players.end()) {
                it->second.hp = max(0, min(hp, it->second.max_hp));
                send(client_sock, "HP set successfully.\n", 22, 0);
            }
            else {
                send(client_sock, "Player not found.\n", 19, 0);
            }
        }
        else if (cmd == "set_view_radius") {
            int target_id, new_radius;
            if (!(iss >> target_id >> new_radius)) {
                send(client_sock, "Usage: /set_view_radius <player_id> <radius>\n", 47, 0);
                return;
            }
            auto it = game_state.players.find(target_id);
            if (it == game_state.players.end()) {
                send(client_sock, "Player not found.\n", 19, 0);
                return;
            }
            if (new_radius < 1 || new_radius > 20) {
                send(client_sock, "Radius must be between 1 and 20.\n", 33, 0);
                return;
            }
            it->second.view_radius = new_radius;
            string msg = "View radius for player " + to_string(target_id) + " set to " + to_string(new_radius) + "\n";
            send(client_sock, msg.c_str(), msg.size(), 0);
        }
        else if (cmd == "reload_scripts") {
            lock.unlock();  // освобождаем перед загрузкой скриптов
            LoadLuaScripts(gLuaState);
            send(client_sock, "Lua scripts reloaded.\n", 22, 0);
            lock.lock();
        }
        else if (cmd == "save_tiles") {
            string filename;
            if (!(iss >> filename)) filename = "tiles.mcgtile";
            lock.unlock();
            save_tiles(filename);
            send(client_sock, ("Tiles saved to " + filename + "\n").c_str(), 0, 0);
            lock.lock();
        }
        else if (cmd == "load_tiles") {
            string filename;
            if (!(iss >> filename)) filename = "tiles.mcgtile";
            lock.unlock();
            load_tiles(filename);
            send(client_sock, ("Tiles loaded from " + filename + "\n").c_str(), 0, 0);
            lock.lock();
        }
        else if (cmd == "tile_set_display") {
            int tile_id;
            string display_str;
            if (!(iss >> tile_id)) {
                send(client_sock, "Usage: /tile_set_display <id> <text>\n", 38, 0);
                return;
            }
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
            if (!(iss >> tile_id >> walkable)) {
                send(client_sock, "Usage: /tile_set_walkable <id> <0/1>\n", 38, 0);
                return;
            }
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
            if (!(iss >> tile_id >> func)) {
                send(client_sock, "Usage: /tile_set_on_enter <id> <lua_func>\n", 43, 0);
                return;
            }
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
            if (!(iss >> tile_id >> func)) {
                send(client_sock, "Usage: /tile_set_on_exit <id> <lua_func>\n", 42, 0);
                return;
            }
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
            if (!(iss >> tile_id >> func)) {
                send(client_sock, "Usage: /tile_set_on_step <id> <lua_func>\n", 42, 0);
                return;
            }
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
            if (!(iss >> tile_id)) {
                send(client_sock, "Usage: /tile_info <id>\n", 24, 0);
                return;
            }
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
            if (!(iss >> tile_id)) {
                send(client_sock, "Usage: /tile_create <id>\n", 26, 0);
                return;
            }
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
            if (!(iss >> filename)) filename = "world.txt";
            lock.unlock();
            save_world_map(filename);
            send(client_sock, ("World map saved to " + filename + "\n").c_str(), 0, 0);
            lock.lock();
        }
        else if (cmd == "reload_map") {
            lock.unlock();
            load_world_map("world.txt");
            send(client_sock, "World map reloaded from world.txt\n", 35, 0);
            lock.lock();
        }
        else if (cmd == "lua_list") {
            lock_guard<mutex> lock(lua_commands_mutex);
            string msg = "\n=== Lua Commands Status ===\n";
            for (const auto& cmd_name : available_lua_commands) {
                msg += (active_lua_commands.count(cmd_name) ? "[ACTIVE] " : "[INACTIVE] ");
                msg += cmd_name;
                {
                    lock_guard<mutex> desc_lock(lua_desc_mutex);
                    auto it = lua_command_descriptions.find(cmd_name);
                    if (it != lua_command_descriptions.end() && !it->second.empty())
                        msg += " - " + it->second;
                }
                msg += "\n";
            }
            msg += "==========================\n";
            send(client_sock, msg.c_str(), msg.size(), 0);
        }
        else if (cmd == "lua_enable") {
            string cmd_name;
            if (!(iss >> cmd_name)) {
                send(client_sock, "Usage: /lua_enable <command_name>\n", 35, 0);
                return;
            }
            bool success = false;
            {
                lock_guard<mutex> lock(lua_commands_mutex);
                if (available_lua_commands.count(cmd_name)) {
                    active_lua_commands.insert(cmd_name);
                    success = true;
                }
            }
            if (success) {
                string ok = "Lua command '" + cmd_name + "' is now ACTIVE.\n";
                send(client_sock, ok.c_str(), ok.size(), 0);
            }
            else {
                string err = "[ERROR] Lua command '" + cmd_name + "' not found.\n";
                send(client_sock, err.c_str(), err.size(), 0);
            }
        }
        else if (cmd == "lua_disable") {
            string cmd_name;
            if (!(iss >> cmd_name)) {
                send(client_sock, "Usage: /lua_disable <command_name>\n", 36, 0);
                return;
            }
            bool removed = false;
            {
                lock_guard<mutex> lock(lua_commands_mutex);
                removed = (active_lua_commands.erase(cmd_name) > 0);
            }
            if (removed) {
                string ok = "Lua command '" + cmd_name + "' is now INACTIVE.\n";
                send(client_sock, ok.c_str(), ok.size(), 0);
            }
            else {
                string err = "[ERROR] Lua command '" + cmd_name + "' is not active.\n";
                send(client_sock, err.c_str(), err.size(), 0);
            }
        }
        else if (cmd == "lua_activate_all") {
            lock_guard<mutex> lock(lua_commands_mutex);
            if (available_lua_commands.empty()) {
                send(client_sock, "No Lua commands available to activate.\n", 39, 0);
            }
            else {
                int count = 0;
                for (const auto& cmd_name : available_lua_commands) {
                    active_lua_commands.insert(cmd_name);
                    count++;
                }
                string msg = "Activated " + to_string(count) + " Lua commands.\n";
                send(client_sock, msg.c_str(), msg.size(), 0);
            }
        }
        else if (cmd == "lua_preset_save") {
            string preset_name;
            if (!(iss >> preset_name)) {
                send(client_sock, "Usage: /lua_preset_save <preset_name>\n", 39, 0);
                return;
            }
            if (preset_name.find_first_of("\\/:*?\"<>|") != string::npos) {
                send(client_sock, "[ERROR] Invalid preset name.\n", 29, 0);
                return;
            }
            string error_msg;
            if (save_lua_preset(preset_name, error_msg)) {
                string ok = "[OK] Preset '" + preset_name + "' saved.\n";
                send(client_sock, ok.c_str(), (int)ok.size(), 0);
            }
            else {
                string err = "[ERROR] " + error_msg + "\n";
                send(client_sock, err.c_str(), (int)err.size(), 0);
            }
        }
        else if (cmd == "lua_preset_load") {
            string preset_name;
            if (!(iss >> preset_name)) {
                send(client_sock, "Usage: /lua_preset_load <preset_name>\n", 39, 0);
                return;
            }
            if (preset_name.find_first_of("\\/:*?\"<>|") != string::npos) {
                send(client_sock, "[ERROR] Invalid preset name.\n", 29, 0);
                return;
            }
            string error_msg;
            if (load_lua_preset(preset_name, error_msg)) {
                string ok = "[OK] Preset '" + preset_name + "' loaded. Use /lua_list to see active commands.\n";
                send(client_sock, ok.c_str(), (int)ok.size(), 0);
            }
            else {
                string err = "[ERROR] " + error_msg + "\n";
                send(client_sock, err.c_str(), (int)err.size(), 0);
            }
        }
        else if (cmd == "lua_preset_list") {
            CreateDirectoryA("presets", NULL);
            WIN32_FIND_DATAA findData;
            HANDLE hFind = FindFirstFileA("presets/*.mcglua", &findData);
            if (hFind == INVALID_HANDLE_VALUE) {
                send(client_sock, "No presets found.\n", 18, 0);
                return;
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
        }
        // ----- Обработчики атрибутов (вызовы) -----
        else if (cmd == "set_attr_all") {
            handle_set_attr_all(client_sock, command);
        }
        else if (cmd == "set_attr") {
            handle_set_attr(client_sock, command, player_id, is_admin);
        }
        else if (cmd == "get_attr") {
            handle_get_attr(client_sock, command);
        }
        else if (cmd == "has_attr") {
            handle_has_attr(client_sock, command);
        }
        else if (cmd == "remove_attr") {
            handle_remove_attr(client_sock, command, player_id, is_admin);
        }
        else if (cmd == "remove_attr_all") {
            handle_remove_attr_all(client_sock, command);
        }
        else if (cmd == "default_attr_add") {
            handle_default_attr_add(client_sock, command);
        }
        else if (cmd == "default_attr_remove") {
            handle_default_attr_remove(client_sock, command);
        }
        else if (cmd == "default_attr_list") {
            handle_default_attr_list(client_sock);
        }
        else if (cmd == "sync_default_attrs") {
            handle_sync_default_attrs(client_sock);
        }
        else if (cmd == "list_attrs") {
            handle_list_attrs(client_sock, command, player_id);
        }
        else if (cmd == "edit_description") {
            handle_edit_description(client_sock, command);
        }
        else if (cmd == "edit_rules") {
            handle_edit_rules(client_sock, command);
        }
        else {
            send(client_sock, "Unknown admin command.\n", 24, 0);
        }
    }
    else {
        send(client_sock, "Unknown command. Type /help for available commands.\n", 55, 0);
    }
    // Блокировка автоматически освободится при выходе
}

// ----- Обработчики атрибутов (без блокировки game_mutex, так как она уже захвачена) -----
void set_attr_for_all(const string& attr_name, const variant<int, float, string, bool>& value) {
    // предполагаем, что game_mutex уже захвачен
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
    // game_mutex уже захвачен
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
    // game_mutex уже захвачен
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
    // game_mutex уже захвачен
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
    // game_mutex уже захвачен
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
    // game_mutex уже захвачен
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
    // game_mutex уже захвачен
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
    // default_attrs_mutex отдельный, game_mutex не трогаем
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
    // Захватываем default_attrs_mutex, game_mutex уже захвачен
    lock_guard<mutex> lock_def(default_attrs_mutex);
    // game_mutex уже захвачен в вызывающем коде

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
    // game_mutex уже захвачен
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd;
    int target_id = caller_id;
    if (!(iss >> target_id)) {
        // оставляем caller_id
    }
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

// ----- Остальные обработчики (не используют game_mutex) -----
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