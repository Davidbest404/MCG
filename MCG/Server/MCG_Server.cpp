#define LUA_BUILD_AS_DLL

#include "lua.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "Ws2_32.lib")

#include "../Common/ConsoleHelper.h"
#include "../Common/Utils.h"
#include "ServerShared.h"
#include "ServerLua.h"
#include "ServerMap.h"
#include "ServerGame.h"
#include "ServerNetwork.h"
#include "ServerCommands.h"

using namespace std;

// ------------------ Определения глобальных переменных ------------------
lua_State* gLuaState = nullptr;
map<string, string> lua_command_descriptions;
mutex lua_desc_mutex;

int PORT = 8080;

GameState game_state;
mutex game_mutex;

vector<SOCKET> clients;
atomic<int> client_count(0);
map<SOCKET, pair<string, int>> client_info;
map<SOCKET, bool> admin_clients;
atomic<int> next_client_id(1);
string Password = "null";
string Name = "Player";
int max_clients = 10;

unordered_map<string, variant<int, float, string, bool>> default_attrs;
mutex default_attrs_mutex;

unordered_set<string> available_lua_commands;
unordered_set<string> active_lua_commands;
mutex lua_commands_mutex;

string server_description = "MCG - multiconsole game!";
string server_rules = "1. Respect other players.\n2. No cheating.\n3. Have fun!";
mutex server_info_mutex;

// (world_tiles и tile_types определены в ServerMap.cpp)

// ------------------ MAIN ------------------
int main() {
    ConsoleHelper::InitConsole();
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
    luaL_openlibs(gLuaState);
    register_lua_functions(gLuaState);
    LoadLuaScripts(gLuaState);

    load_world_map("world.txt");
    load_tiles("tiles.mcgtile");

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