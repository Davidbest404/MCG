#include <iostream>
#include <random>
#include <fstream>
#include <cstring>
#include <thread>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <atomic>
#include <ctime>
#include <mutex>
#include <sstream>

// Только Windows
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

const int PORT = 8080;

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

enum class ActionType {
    MOVE,
    ATTACK,
    DEFEND,
    USE_ITEM,
    WAIT,
    SKIP
};

struct Player {
    string name;
    int id;
    bool is_admin;
    bool is_ready = false;
    ActionType last_action = ActionType::WAIT;
    int hp = 100;
    int max_hp = 100;
    int level = 1;
    int exp = 0;
    int x = 0;
    int y = 0;
    int gold = 0;
};

struct GameState {
    bool is_active = false;
    time_t turn_start_time;
    int turn_duration_seconds = 1800;
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
string Name = "Chat";
int max_clients = 10;
GameState game_state;
mutex game_mutex;

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
            broadcast_message("\n=== Turn automatically ended by timer ===\n", INVALID_SOCKET);
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
        case ActionType::ATTACK:
            turn_summary += p.name + " attacked!\n";
            break;
        case ActionType::DEFEND:
            turn_summary += p.name + " is defending.\n";
            break;
        case ActionType::USE_ITEM:
            turn_summary += p.name + " used an item.\n";
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
        command.find("/set_") != 0 && command != "/status") {
        send(client_sock, "Game is not active. Admin must start the game.\n", 52, 0);
        return;
    }

    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd;

    if (cmd == "move") {
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
    else if (cmd == "attack") {
        int target_id;
        iss >> target_id;
        if (game_state.players.find(player_id) == game_state.players.end() ||
            game_state.players.find(target_id) == game_state.players.end()) {
            send(client_sock, "Player or target not found!\n", 28, 0);
            return;
        }
        auto& attacker = game_state.players[player_id];
        auto& target = game_state.players[target_id];
        if (attacker.is_ready) {
            send(client_sock, "You already made your move this turn!\n", 40, 0);
            return;
        }
        int damage = 10 + (attacker.level * 2);
        target.hp -= damage;
        if (target.hp < 0) {
            target.hp = 100;
            target.x = 0;
            target.y = 0;
        }
        attacker.last_action = ActionType::ATTACK;
        attacker.is_ready = true;
        attacker.exp += 5;

        bool level_up = false;
        if (attacker.exp >= 100) {
            attacker.level++;
            attacker.exp = 0;
            attacker.max_hp += 20;
            attacker.hp = attacker.max_hp;
            level_up = true;
        }

        string response = "You will attack " + target.name + " for " +
            to_string(Random(damage, damage / 2)) + " damage!\n";
        if (target.hp <= 0) {
            response += target.name + " has been defeated!\n";
            attacker.gold += 50;
            attacker.exp += 20;
            response += "You gained 50 gold and 20 XP!\n";
        }
        send(client_sock, response.c_str(), static_cast<int>(response.length()), 0);

        string broadcast_msg = attacker.name + " attacked " + target.name +
            " for " + to_string(damage) + " damage!";
        string level_up_msg = attacker.name + " reached level " + to_string(attacker.level) + "!";

        lock.unlock();
        broadcast_message(broadcast_msg, client_sock);
        if (level_up) broadcast_message(level_up_msg, INVALID_SOCKET);
    }
    else if (cmd == "defend") {
        if (game_state.players.find(player_id) == game_state.players.end()) {
            send(client_sock, "Player not found!\n", 19, 0);
            return;
        }
        auto& player = game_state.players[player_id];
        player.last_action = ActionType::DEFEND;
        send(client_sock, "You are defending this turn.\n", 30, 0);
    }
    else if (cmd == "use") {
        send(client_sock, "Item used!\n", 11, 0);
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
        string status = "\n=== Your Status ===\n";
        status += "Name: " + player.name + "\n";
        status += "HP: " + to_string(player.hp) + "/" + to_string(player.max_hp) + "\n";
        status += "Level: " + to_string(player.level) + "\n";
        status += "XP: " + to_string(player.exp) + "/100\n";
        status += "Gold: " + to_string(player.gold) + "\n";
        status += "Position: (" + to_string(player.x) + "," + to_string(player.y) + ")\n";
        status += "Ready: " + string(player.is_ready ? "Yes" : "No") + "\n";
        status += "==================\n";
        send(client_sock, status.c_str(), static_cast<int>(status.length()), 0);
    }
    else if (cmd == "map") {
        string map = "\n=== Game Map ===\n";
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
            string broadcast_msg = "=== GAME STARTED ===\nTurn duration: " +
                to_string(game_state.turn_duration_seconds / 60) + " minutes\n";
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
            string broadcast_msg = "Turn duration set to " + to_string(seconds / 60) + " minutes.";
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
    }
    else {
        send(client_sock, "Unknown command. Type /help for available commands.\n", 55, 0);
    }
}

void save_game_state(const string& filename) {
    ofstream file(filename);
    if (!file.is_open()) {
        cerr << "Failed to save game state to " << filename << endl;
        return;
    }
    lock_guard<mutex> lock(game_mutex);
    file << game_state.current_turn << endl;
    file << game_state.turn_duration_seconds << endl;
    file << game_state.is_active << endl;
    file << game_state.players.size() << endl;
    for (auto& pair : game_state.players) {
        auto& player = pair.second;
        file << player.name << " "
            << player.hp << " "
            << player.max_hp << " "
            << player.level << " "
            << player.exp << " "
            << player.x << " "
            << player.y << " "
            << player.gold << " "
            << player.is_admin << endl;
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
            >> player.level
            >> player.exp
            >> player.x
            >> player.y
            >> player.gold
            >> player.is_admin;
        int new_id = next_client_id++;
        player.id = new_id;
        game_state.players[new_id] = player;
        ConsoleHelper::SetColor(4);
        cout << "Loaded player: " << player.name
            << " (HP: " << player.hp << "/" << player.max_hp
            << ", Level: " << player.level << ")" << endl;
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
            save_game_state("autosave.rpg");
            ConsoleHelper::SetColor(10);
            cout << "Auto-save completed" << endl;
            ConsoleHelper::SetColor(8);
        }
    }
}

void handle_client(SOCKET client_sock) {
    char buffer[1024];
    cout << "Client connected: socket " << client_sock << endl;

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
            string error = "ERROR|Invalid format. Use: AUTH|username|password\n";
            send(client_sock, error.c_str(), static_cast<int>(error.length()), 0);
        }
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
                    string help_msg = "\n=== Available Commands ===\n";
                    help_msg += "/help - Show this message\n/list - List all connected clients\n";
                    {
                        lock_guard<mutex> lock(game_mutex);
                        if (game_state.players.find(client_info[client_sock].second) != game_state.players.end()) {
                            help_msg += "/move [direction] - Move (north, south, east, west)\n";
                            help_msg += "/attack [target_id] - Attack another player\n";
                            help_msg += "/defend - Defend yourself\n/skip - Skip your turn\n";
                            help_msg += "/status - Check your status\n/map - Show game map\n";
                            help_msg += "/ready - Mark yourself as ready\n/unready - Mark yourself as not ready\n";
                        }
                    }
                    if (is_admin) {
                        help_msg += "/kick [id] - Kick a client\n/name [new_name] - Change server name\n";
                        help_msg += "/max [number] - Change max clients\n/info - Show server info\n";
                        help_msg += "/clients - Show detailed client info\n/start_game - Start the game\n";
                        help_msg += "/pause_game - Pause the game\n/set_turn_time [seconds] - Set turn duration\n";
                        help_msg += "/end_turn - Force end current turn\n/save [filename] - Save game state\n";
                        help_msg += "/load [filename] - Load game state\n";
                    }
                    help_msg += "========================\n";
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
                                    list_msg += " | Level: " + to_string(player.level);
                                }
                            }
                            list_msg += "\n";
                        }
                    }
                    list_msg += "==============================\n";
                    send(client_sock, list_msg.c_str(), static_cast<int>(list_msg.length()), 0);
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
                                        string error = "ERROR|Cannot kick another admin\n";
                                        send(client_sock, error.c_str(), static_cast<int>(error.length()), 0);
                                        found = true;
                                    }
                                }
                            }
                            if (!found) {
                                string error = "ERROR|Client not found\n";
                                send(client_sock, error.c_str(), static_cast<int>(error.length()), 0);
                            }
                        }
                        catch (...) {
                            string error = "ERROR|Invalid client ID\n";
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
                                string error = "ERROR|Max clients must be between 1 and 1000\n";
                                send(client_sock, error.c_str(), static_cast<int>(error.length()), 0);
                            }
                        }
                        catch (...) {
                            string error = "ERROR|Invalid number\n";
                            send(client_sock, error.c_str(), static_cast<int>(error.length()), 0);
                        }
                        continue;
                    }
                    else if (message == "/info") {
                        string info_msg = "\n=== Server Information ===\n";
                        info_msg += "Name: " + Name + "\nPort: " + to_string(PORT) + "\n";
                        info_msg += "Max clients: " + to_string(max_clients) + "\n";
                        info_msg += "Connected clients: " + to_string(client_count) + "\n";
                        info_msg += "Config password: " + Password + "\n";
                        info_msg += "Game active: " + string(game_state.is_active ? "Yes" : "No") + "\n";
                        info_msg += "Current turn: " + to_string(game_state.current_turn) + "\n";
                        info_msg += "Turn duration: " + to_string(game_state.turn_duration_seconds / 60) + " minutes\n";
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
                        broadcast_message("=== GAME STARTED ===\nTurn duration: " + to_string(game_state.turn_duration_seconds / 60) + " minutes\n", INVALID_SOCKET);
                        continue;
                    }
                    else if (message == "/pause_game") {
                        game_state.is_active = false;
                        broadcast_message("Game paused by admin.", INVALID_SOCKET);
                        continue;
                    }
                    else if (message.length() > 14 && message.substr(0, 14) == "/set_turn_time") {
                        try {
                            int seconds = stoi(message.substr(15));
                            game_state.turn_duration_seconds = seconds;
                            broadcast_message("Turn duration set to " + to_string(seconds / 60) + " minutes.", INVALID_SOCKET);
                        }
                        catch (...) {
                            string error = "ERROR|Invalid number\n";
                            send(client_sock, error.c_str(), static_cast<int>(error.length()), 0);
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
                }
                int player_id = client_info[client_sock].second;
                {
                    if (game_state.players.find(player_id) != game_state.players.end()) {
                        process_game_command(client_sock, message, player_id, is_admin);
                        continue;
                    }
                }
                string error = "ERROR|Unknown command. Type /help for available commands\n";
                send(client_sock, error.c_str(), static_cast<int>(error.length()), 0);
                continue;
            }
            auto& info = client_info[client_sock];
            string full_msg = "[" + to_string(info.second) + "] " + info.first;
            if (is_admin) full_msg += " [ADMIN]";
            full_msg += ": " + message;
            cout << "Message: " << full_msg << endl;
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

int main() {
    cout << "=== Cross-platform RPG Game Server (Windows) ===" << endl;
    if (!network_init()) return 1;

    cout << "Enter max clients: ";
    cin >> max_clients;
    cout << "Enter configuration password: ";
    cin >> Password;
    cout << "Enter server name: ";
    cin >> Name;

    char load_choice;
    cout << "Load saved game? (y/n): ";
    cin >> load_choice;
    if (load_choice == 'y' || load_choice == 'Y') {
        string save_file;
        cout << "Enter save file name (default: game_save.rpg): ";
        cin >> save_file;
        if (save_file.empty()) save_file = "game_save.rpg";
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

    if (bind(server_sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
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

    cout << "\n=== Server Information ===" << endl;
    cout << "Name: " << Name << endl;
    cout << "Platform: Windows" << endl;
    cout << "Port: " << PORT << endl;
    cout << "Max clients: " << max_clients << endl;
    cout << "Server IP: 0.0.0.0 (all interfaces)" << endl;
    cout << "Localhost: 127.0.0.1:" << PORT << endl;
    cout << "Game system: RPG Turn-based" << endl;
    cout << "Default turn time: " << game_state.turn_duration_seconds / 60 << " minutes" << endl;
    cout << "Waiting for connections..." << endl;
    cout << "Press Ctrl+C to stop server" << endl;
    cout << "==========================\n" << endl;

    while (true) {
        sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        SOCKET client_sock = accept(server_sock, (sockaddr*)&client_addr, &client_len);
        if (client_sock == INVALID_SOCKET) {
            cerr << "Accept failed: " << WSAGetLastError() << endl;
            continue;
        }
        if (client_count >= max_clients) {
            string error = "ERROR|Server is full. Max: " + to_string(max_clients);
            send(client_sock, error.c_str(), static_cast<int>(error.length()), 0);
            closesocket(client_sock);
            cout << "Connection rejected: server full (" << max_clients << " clients)" << endl;
            continue;
        }
        string welcome_msg = "=== Connected to " + Name + " RPG Server ===\n";
        welcome_msg += "Authentication required.\nFormat: AUTH|username|password\n==================================\n";
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