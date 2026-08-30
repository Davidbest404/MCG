#include "ServerNetwork.h"
#include "ServerShared.h"
#include "ServerMap.h"
#include "ServerGame.h"
#include "ServerCommands.h"
#include "ServerLua.h"
#include "../Common/ConsoleHelper.h"
#include <iostream>
#include <algorithm>
#include <thread>
#include <sstream>

using namespace std;

// Внешние зависимости
extern GameState game_state;
extern mutex game_mutex;
extern vector<SOCKET> clients;
extern atomic<int> client_count;
extern map<SOCKET, pair<string, int>> client_info;
extern map<SOCKET, bool> admin_clients;
extern atomic<int> next_client_id;
extern string Password, Name;
extern int max_clients;
extern string server_description, server_rules;
extern mutex server_info_mutex;
extern unordered_set<string> available_lua_commands;
extern unordered_set<string> active_lua_commands;
extern mutex lua_commands_mutex;
extern map<string, string> lua_command_descriptions;
extern mutex lua_desc_mutex;

// ----- Реализации -----

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
                    int start_x = 0, start_y = 0;
                    if (is_walkable(0, 0)) {
                        player.x = 0; player.y = 0;
                    }
                    else {
                        if (find_nearest_walkable(0, 0, start_x, start_y)) {
                            player.x = start_x; player.y = start_y;
                        }
                        else {
                            player.x = 0; player.y = 0;
                        }
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
                int player_id = client_info[client_sock].second;
                // Все команды отправляем в общий обработчик
                process_game_command(client_sock, message, player_id, is_admin);
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