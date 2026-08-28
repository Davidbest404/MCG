#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <cstring>
#include <thread>
#include <string>
#include <atomic>
#include <vector>
#include <queue>
#include <mutex>
#include <map>
#include <sstream>
#include <algorithm>
#include <memory>
#include <chrono>
#include <iomanip>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>

#pragma comment(lib, "Ws2_32.lib")

#include "../Common/ConsoleHelper.h"
#include "../Common/ColorParser.h"
#include "../Common/Utils.h"

using namespace std;

int SERVER_PORT = 8080;
int LOCAL_PORT = 9090;

atomic<bool> running(true);
atomic<bool> connected_to_game(false);
atomic<bool> local_server_running(false);
atomic<bool> show_coordinates(true);

atomic<int> chat_windows_count(0);
atomic<int> map_windows_count(0);
atomic<int> status_windows_count(0);

mutex cout_mutex;

enum class ClientType {
    CHAT_WINDOW,
    MAP_WINDOW,
    STATUS_WINDOW,
    UNKNOWN
};

struct LocalClient {
    SOCKET socket;
    ClientType type;
    string name;
};

SOCKET game_socket = INVALID_SOCKET;
vector<LocalClient> local_clients;
mutex local_clients_mutex;

mutex color_remap_mutex;
int color_remap[16];

mutex bg_color_remap_mutex;
int bg_color_remap[16];

// ---------- Прототипы функций ----------
bool safe_send(SOCKET sock, const string& data);
string safe_receive(SOCKET sock, int timeout_ms = 1000);

// ---------- Система биндов ----------
struct Bind {
    string command;
    int interval;
    chrono::steady_clock::time_point last_send;
};
map<string, Bind> binds;
mutex binds_mutex;
atomic<bool> binds_running(true);
thread binds_thread;

void binds_loop() {
    while (binds_running && running) {
        this_thread::sleep_for(chrono::seconds(1));
        auto now = chrono::steady_clock::now();
        lock_guard<mutex> lock(binds_mutex);
        for (auto& [name, bind] : binds) {
            if (bind.interval > 0 && connected_to_game) {
                auto elapsed = chrono::duration_cast<chrono::seconds>(now - bind.last_send).count();
                if (elapsed >= bind.interval) {
                    if (game_socket != INVALID_SOCKET) {
                        safe_send(game_socket, bind.command);
                    }
                    bind.last_send = now;
                }
            }
        }
    }
}

// ---------- Ремаппинг цветов ----------
int get_remapped_color(char hex_char) {
    int idx = hex_char_to_int(hex_char);
    if (idx < 0 || idx >= 16) return idx;
    lock_guard<mutex> lock(color_remap_mutex);
    return color_remap[idx];
}

int get_remapped_bg_color(char hex_char) {
    int idx = hex_char_to_int(hex_char);
    if (idx < 0 || idx >= 16) return idx;
    lock_guard<mutex> lock(bg_color_remap_mutex);
    return bg_color_remap[idx];
}

// ---------- Вывод ----------
void output_message(const string& message) {
    lock_guard<mutex> lock(cout_mutex);
    print_colored_text(message);
    cout << endl;
}

// ---------- Сокеты ----------
bool safe_send(SOCKET sock, const string& data) {
    if (sock == INVALID_SOCKET) return false;
    int result = send(sock, data.c_str(), static_cast<int>(data.size()), 0);
    return result > 0;
}

string safe_receive(SOCKET sock, int timeout_ms) {
    if (sock == INVALID_SOCKET) return "";
    DWORD timeout = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        return string(buffer);
    }
    return "";
}

// ---------- Типы сообщений ----------
enum class MessageType {
    CHAT_MESSAGE,
    MAP_UPDATE,
    STATUS_UPDATE,
    COMMAND_RESPONSE,
    ERROR_RESPONSE,
    SYSTEM_MESSAGE
};

MessageType get_message_type(const string& message) {
    if (message.find("[MAP]") != string::npos) return MessageType::MAP_UPDATE;
    else if (message.find("[STATUS]") != string::npos) return MessageType::STATUS_UPDATE;
    else if (message.find("[SERVER]") != string::npos || message.find("[CHAT]") != string::npos)
        return MessageType::CHAT_MESSAGE;
    else if (message.find("[COMMAND]") != string::npos) return MessageType::COMMAND_RESPONSE;
    else if (message.find("[LUA]") != string::npos) return MessageType::COMMAND_RESPONSE;
    else if (message.find("[ERROR]") != string::npos) return MessageType::COMMAND_RESPONSE;
    return MessageType::SYSTEM_MESSAGE;
}

// ---------- Отправка локальным клиентам ----------
void send_to_local_clients(const string& message, ClientType target_type = ClientType::UNKNOWN) {
    bool sent = false;
    {
        lock_guard<mutex> lock(local_clients_mutex);
        for (const auto& client : local_clients) {
            if (target_type == ClientType::UNKNOWN || client.type == target_type) {
                if (safe_send(client.socket, message + "\n")) sent = true;
            }
        }
    }
    if (!sent) {
        lock_guard<mutex> lock(cout_mutex);
        if (target_type == ClientType::CHAT_WINDOW) cout << "[CHAT] ";
        else if (target_type == ClientType::MAP_WINDOW) cout << "[MAP]\n";
        else if (target_type == ClientType::STATUS_WINDOW) cout << "[STATUS]\n";
        print_colored_text(message);
        if (!message.empty() && message.back() != '\n') cout << '\n';
    }
}

// ---------- Отрисовка карты с фиксированной шириной ячейки ----------
void render_map_from_data(const string& data) {
    const int CELL_WIDTH = 3; // фиксированная ширина для каждой ячейки

    size_t pos1 = data.find('|');
    if (pos1 == string::npos) return;
    string player_info = data.substr(0, pos1);
    string objects_str = data.substr(pos1 + 1);

    vector<string> parts;
    stringstream ps(player_info);
    string part;
    while (getline(ps, part, ';')) {
        parts.push_back(part);
    }
    if (parts.size() < 3) return;
    int my_id = stoi(parts[0]);
    int player_x = stoi(parts[1]);
    int player_y = stoi(parts[2]);
    int radius = (parts.size() >= 4) ? stoi(parts[3]) : 10;

    struct MapObject {
        int id;
        int x, y;
        char type;
        char symbol;
    };
    vector<MapObject> objects;

    stringstream ss(objects_str);
    string obj;
    while (getline(ss, obj, '|')) {
        if (obj.empty()) continue;
        stringstream o(obj);
        string id_str, x_str, y_str, type_str;
        if (!getline(o, id_str, ';')) continue;
        if (!getline(o, x_str, ';')) continue;
        if (!getline(o, y_str, ';')) continue;
        if (!getline(o, type_str)) continue;

        MapObject mo;
        mo.id = stoi(id_str);
        mo.x = stoi(x_str);
        mo.y = stoi(y_str);
        if (type_str == "P") {
            mo.type = 'P';
            mo.symbol = 0;
        }
        else if (type_str.rfind("T:", 0) == 0) {
            mo.type = 'T';
            mo.symbol = type_str[2];
        }
        else {
            continue;
        }
        objects.push_back(mo);
    }

    int min_x = player_x - radius;
    int max_x = player_x + radius;
    int min_y = player_y - radius;
    int max_y = player_y + radius;

    stringstream map_text;
    map_text << "[MAP]\n[c1][bg4] === Game Map === [/bg4][/c1]\n";

    // Функция для преобразования ID игрока в символ
    auto id_to_char = [](int id) -> char {
        if (id < 10) return '0' + id;
        else if (id < 36) return 'A' + (id - 10);
        else return '?';
        };

    // Верхняя строка с координатами X
    if (show_coordinates) {
        // Отступ для левой колонки (такой же, как ширина ячейки)
        map_text << string(CELL_WIDTH, ' ');
        for (int x = min_x; x <= max_x; ++x) {
            // Выводим число с выравниванием по правому краю в ширину CELL_WIDTH
            map_text << setw(CELL_WIDTH) << right << x;
        }
        map_text << "\n";
    }

    for (int y = max_y; y >= min_y; --y) {
        // Левая колонка с координатой Y
        if (show_coordinates) {
            map_text << setw(CELL_WIDTH) << right << y << "  ";
        }

        for (int x = min_x; x <= max_x; ++x) {
            char cell_char = '.';
            bool is_current_player = false;
            bool found = false;

            for (const auto& obj : objects) {
                if (obj.x == x && obj.y == y) {
                    if (obj.type == 'P') {
                        if (obj.id == my_id) {
                            is_current_player = true;
                        }
                        cell_char = id_to_char(obj.id);
                        found = true;
                        break;
                    }
                    else if (obj.type == 'T') {
                        cell_char = obj.symbol;
                        found = true;
                        break;
                    }
                }
            }

            // Формируем строку для ячейки с фиксированной шириной CELL_WIDTH
            string cell_str;
            if (is_current_player) {
                // Выделяем текущего игрока цветом
                cell_str = "[cA]" + string(1, cell_char) + "[/cA]";
                // Добавляем пробелы до нужной ширины (после закрывающего тега)
                int visible_len = 1; // только символ, теги невидимы
                int spaces = CELL_WIDTH - visible_len;
                cell_str += string(spaces, ' ');
            }
            else {
                cell_str = string(1, cell_char) + string(CELL_WIDTH - 1, ' ');
            }

            map_text << cell_str;
        }
        map_text << "\n";
    }

    map_text << "================\n";
    map_text << "[c8]Your position: (" << player_x << "," << player_y << ")[/c8]\n";
    if (show_coordinates) {
        map_text << "[c8]Coordinates shown.[/c8]\n";
    }
    else {
        map_text << "[c8]Coordinates hidden (use //toggle_coords)[/c8]\n";
    }

    lock_guard<mutex> lock(cout_mutex);
    print_colored_text(map_text.str());
}

// ---------- Обработка сообщений ----------
void process_game_message(const string& message) {
    if (message.empty()) return;

    if (message.find("[MAP]\nMAP_DATA|") != string::npos) {
        size_t start = message.find("MAP_DATA|") + 9;
        size_t end = message.find('\n', start);
        if (end == string::npos) end = message.length();
        string data = message.substr(start, end - start);
        render_map_from_data(data);
        return;
    }

    MessageType type = get_message_type(message);

    bool has_local = false;
    {
        lock_guard<mutex> lock(local_clients_mutex);
        for (const auto& client : local_clients) {
            if (type == MessageType::CHAT_MESSAGE && client.type == ClientType::CHAT_WINDOW) {
                safe_send(client.socket, message + "\n");
                has_local = true;
            }
            else if (type == MessageType::MAP_UPDATE && client.type == ClientType::MAP_WINDOW) {
                safe_send(client.socket, message + "\n");
                has_local = true;
            }
            else if (type == MessageType::STATUS_UPDATE && client.type == ClientType::STATUS_WINDOW) {
                safe_send(client.socket, message + "\n");
                has_local = true;
            }
        }
    }

    if (!has_local) {
        output_message(message);
    }
}

// ---------- Приём от сервера ----------
void receive_from_game_server() {
    char buffer[4096];
    while (running && connected_to_game) {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(game_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            process_game_message(string(buffer));
        }
        else if (bytes_received == 0) {
            output_message("[SYSTEM] Disconnected from game server");
            connected_to_game = false;
            break;
        }
        this_thread::sleep_for(chrono::milliseconds(10));
    }
}

// ---------- Подключение к серверу ----------
bool connect_to_game_server(const string& ip_address, int& port) {
    game_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (game_socket == INVALID_SOCKET) {
        output_message("[ERROR] Failed to create socket");
        return false;
    }
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip_address.c_str());
    if (server_addr.sin_addr.s_addr == INADDR_NONE) {
        if (ip_address == "localhost" || ip_address == "127.0.0.1") {
            server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        }
        else {
            output_message("[ERROR] Invalid IP address or port: " + ip_address + to_string(port));
            closesocket(game_socket);
            game_socket = INVALID_SOCKET;
            return false;
        }
    }
    output_message("[SYSTEM] Connecting to game server at " + ip_address + ":" + to_string(port) + "...");
    if (connect(game_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        output_message("[ERROR] Connection failed. Make sure server is running.");
        closesocket(game_socket);
        game_socket = INVALID_SOCKET;
        return false;
    }
    return true;
}

// ---------- Локальный сервер для окон ----------
void local_server() {
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) return;
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(LOCAL_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "\n[ERROR] Failed to bind local server to port " << LOCAL_PORT << endl;
        closesocket(server_socket);
        return;
    }
    if (listen(server_socket, 5) < 0) {
        cerr << "\n[ERROR] Failed to listen on port " << LOCAL_PORT << endl;
        closesocket(server_socket);
        return;
    }
    local_server_running = true;
    while (running && local_server_running) {
        sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &addr_len);
        if (client_socket == INVALID_SOCKET) continue;
        string client_info_str = safe_receive(client_socket, 1000);
        LocalClient new_client;
        new_client.socket = client_socket;
        if (client_info_str.find("CHAT_WINDOW") != string::npos) {
            new_client.type = ClientType::CHAT_WINDOW;
            new_client.name = "Chat Window";
            chat_windows_count++;
        }
        else if (client_info_str.find("MAP_WINDOW") != string::npos) {
            new_client.type = ClientType::MAP_WINDOW;
            new_client.name = "Map Window";
            map_windows_count++;
        }
        else if (client_info_str.find("STATUS_WINDOW") != string::npos) {
            new_client.type = ClientType::STATUS_WINDOW;
            new_client.name = "Status Window";
            status_windows_count++;
        }
        else {
            new_client.type = ClientType::UNKNOWN;
            new_client.name = "Unknown Window";
            safe_send(client_socket, "[ERROR] Unknown window type. Use: CHAT_WINDOW, MAP_WINDOW, or STATUS_WINDOW\n");
            closesocket(client_socket);
            continue;
        }
        {
            lock_guard<mutex> lock(local_clients_mutex);
            local_clients.push_back(new_client);
        }
        cout << "\n[SYSTEM] " << new_client.name << " connected" << endl;
        string welcome = "=== Connected to MCG Client ===\nWindow: " + new_client.name + "\n";
        welcome += "You will receive relevant updates here.\n==================================\n\n";
        safe_send(client_socket, welcome);
    }
    closesocket(server_socket);
}

// ---------- Очистка локальных клиентов ----------
void cleanup_local_clients() {
    lock_guard<mutex> lock(local_clients_mutex);
    for (auto& client : local_clients) {
        if (client.socket != INVALID_SOCKET) {
            if (client.type == ClientType::CHAT_WINDOW) chat_windows_count--;
            else if (client.type == ClientType::MAP_WINDOW) map_windows_count--;
            else if (client.type == ClientType::STATUS_WINDOW) status_windows_count--;
            closesocket(client.socket);
        }
    }
    local_clients.clear();
}

// ---------- Таблица цветов ----------
void show_color_table() {
    const char* color_names[] = {
        "Black", "Blue", "Green", "Cyan", "Red", "Purple", "Brown", "Light gray",
        "Dark gray", "Light blue", "Light green", "Light cyan", "Light red", "Light purple",
        "Yellow", "White"
    };
    const char hex_chars[] = "0123456789ABCDEF";

    lock_guard<mutex> lock(cout_mutex);
    cout << "\n============== Current Color Mapping ===============\n";
    cout << "Color\t\tCode\tColor Set\tBG Color Set\n";
    cout << "----------------------------------------------------\n";
    for (int i = 0; i < 16; ++i) {
        ConsoleHelper::SetColor(7, i);
        printf(" ");
        if (i == 0) {
            ConsoleHelper::SetColor(i, 15 - i);
        }
        else {
            ConsoleHelper::SetColor(i);
        }
        printf("%-12s", color_names[i]);
        ConsoleHelper::ResetColor();
        printf("\t%c\t%d\t\t%d\n", hex_chars[i], color_remap[i], bg_color_remap[i]);
    }
    cout << "====================================================\n";
    cout << "Use: //change_colors <color_name> <new_hex_code>\n";
    cout << "  or //change_bg_colors <color_name> <new_hex_code>\n";
    cout << "Example: //change_colors White 0 - would make White text color into black\n";
    cout << "Use //reset_colors to restore defaults.\n\n";
}

// ---------- Основной ввод команд ----------
void input_loop() {
    cout << "Type '//help' for commands or '//windows' for window setup instructions.\n";
    while (running) {
        this_thread::sleep_for(chrono::milliseconds(30));
        ConsoleHelper::SetColor(15);
        cout << "|";
        ConsoleHelper::SetColor(7);
        cout << ">";
        ConsoleHelper::SetColor(8);
        cout << "- ";
        ConsoleHelper::ResetColor();
        string command;
        getline(cin, command);
        if (!running) break;

        if (command.empty()) continue;

        if (command.find("//") == 0) {
            // ---------- Внутренние команды клиента ----------
            if (command == "//exit") {
                running = false;
                break;
            }
            else if (command == "//connect") {
                if (connected_to_game) {
                    output_message("[ERROR] Already connected to game server. Disconnect first (//disconnect)");
                    continue;
                }
                string ip, portStr;
                int port;
                cout << "Server IP [127.0.0.1]: ";
                getline(cin, ip);
                if (ip.empty()) ip = "127.0.0.1";
                if (!is_valid_ip(ip)) {
                    output_message("[ERROR] Invalid IP address format.");
                    continue;
                }
                cout << "Server port [" << SERVER_PORT << "]: ";
                getline(cin, portStr);
                if (portStr.empty()) port = SERVER_PORT;
                else if (!is_valid_port(portStr, port)) {
                    output_message("[ERROR] Invalid port number (must be 1-65535).");
                    continue;
                }
                if (connect_to_game_server(ip, port)) {
                    connected_to_game = true;
                    thread receive_thread(receive_from_game_server);
                    receive_thread.detach();
                }
            }
            else if (command == "//disconnect") {
                if (connected_to_game) {
                    closesocket(game_socket);
                    game_socket = INVALID_SOCKET;
                    connected_to_game = false;
                    send_to_local_clients("[SYSTEM] Disconnected from game server", ClientType::CHAT_WINDOW);
                    output_message("[SYSTEM] Disconnected from game server");
                }
                else {
                    output_message("[ERROR] You are not connected to game server");
                }
            }
            else if (command == "//windows") {
                cout << "\n======= Window Connection Instructions ======\n";
                cout << "Local server is running on port " << LOCAL_PORT << "\n";
                ConsoleHelper::SetColor(8);
                cout << "For each window, use the same port ^^^:\n";
                ConsoleHelper::ResetColor();
                cout << "---------------------------------------------\n";
                cout << "1. Open Explorer Folder\n";
                cout << "2. Navigate to folder with this game\n";
                cout << "3. Run the appropriate window client:\n";
                cout << "   - Chat Window:    chat_window.exe\n";
                cout << "   - Map Window:     map_window.exe\n";
                cout << "   - Status Window:  status_window.exe\n\n";
                cout << "=============================================\n\n";
                cout << "\n========= Connected Windows Status ==========\n";
                cout << "Chat Windows:   " << chat_windows_count;
                ConsoleHelper::SetColor(8);
                cout << " (messages will go here)\n";
                ConsoleHelper::ResetColor();
                cout << "Map Windows:    " << map_windows_count;
                ConsoleHelper::SetColor(8);
                cout << " (map updates will go here)\n";
                ConsoleHelper::ResetColor();
                cout << "Status Windows: " << status_windows_count;
                ConsoleHelper::SetColor(8);
                cout << " (status updates will go here)\n";
                ConsoleHelper::ResetColor();
                cout << "=============================================\n\n";
                if (chat_windows_count == 0)
                    output_message("Note: No chat windows open. Chat messages will appear in this console.");
                if (map_windows_count == 0)
                    output_message("Note: No map windows open. Map updates will appear in this console.");
                if (status_windows_count == 0)
                    output_message("Note: No status windows open. Status updates will appear in this console.");
            }
            else if (command == "//help") {
                cout << "\n=== MCG Client Commands ===\n";
                cout << "//help        - Shows this help\n";
                cout << "//exit        - Closes client\n";
                cout << "//connect     - Connect to game server\n";
                cout << "//disconnect  - Disconnect from game server\n";
                cout << "//windows     - Show window connection instructions\n";
                cout << "//clients     - Show connected windows\n";
                cout << "//change_colors  - Show color mapping table\n";
                cout << "          add   [name] [code] - To remap a color\n";
                cout << "//change_bg_colors  - Show color mapping table\n";
                cout << "           add     [name] [code] - To remap a bg color\n";
                cout << "//reset_colors   - Restore default color mapping\n";
                cout << "//toggle_coords - Toggle coordinate display on map\n";
                cout << "\n--- Bind commands ---\n";
                cout << "//bind <name> <command> [interval] - Create a bind (command must start with /)\n";
                cout << "   interval in seconds, if omitted – manual only (no timer)\n";
                cout << "//unbind <name> - Remove a bind\n";
                cout << "//binds        - List all binds\n";
                cout << "//send <name>  - Send the bind command immediately\n";
                cout << "=============================\n";
            }
            else if (command == "//clients") {
                lock_guard<mutex> lock(local_clients_mutex);
                cout << "\n=== Connected Windows ===\n";
                cout << "Count: " << local_clients.size() << "\n";
                for (const auto& client : local_clients) {
                    cout << "  - " << client.name;
                    if (client.type == ClientType::CHAT_WINDOW) cout << " (Chat)";
                    else if (client.type == ClientType::MAP_WINDOW) cout << " (Map)";
                    else if (client.type == ClientType::STATUS_WINDOW) cout << " (Status)";
                    cout << "\n";
                }
                cout << "==========================\n\n";
            }
            else if (command == "//toggle_coords") {
                show_coordinates = !show_coordinates;
                output_message("[SYSTEM] Coordinates " + string(show_coordinates ? "enabled" : "disabled"));
            }
            // ---------- Команды биндов ----------
            else if (command.find("//bind") == 0) {
                string rest = command.size() > 6 ? command.substr(7) : "";
                size_t start = rest.find_first_not_of(" \t");
                if (start == string::npos) {
                    output_message("[ERROR] Usage: //bind <name> <command> [interval]");
                    continue;
                }
                rest = rest.substr(start);
                stringstream ss(rest);
                string name, cmd, interval_str;
                if (!(ss >> name)) {
                    output_message("[ERROR] Missing bind name");
                    continue;
                }
                if (!(ss >> cmd)) {
                    output_message("[ERROR] Missing command");
                    continue;
                }
                if (cmd[0] != '/') cmd = "/" + cmd;
                int interval = 0;
                if (ss >> interval_str) {
                    try {
                        interval = stoi(interval_str);
                        if (interval < 0) interval = 0;
                    }
                    catch (...) {
                        output_message("[ERROR] Invalid interval, must be a positive number");
                        continue;
                    }
                }

                {
                    lock_guard<mutex> lock(binds_mutex);
                    Bind bind;
                    bind.command = cmd;
                    bind.interval = interval;
                    bind.last_send = chrono::steady_clock::now();
                    binds[name] = bind;
                }
                output_message("[SYSTEM] Bind '" + name + "' created: command = " + cmd + (interval > 0 ? ", interval = " + to_string(interval) + "s" : ", manual only"));
            }
            else if (command.find("//unbind") == 0) {
                string name = command.size() > 8 ? command.substr(9) : "";
                if (name.empty()) {
                    output_message("[ERROR] Usage: //unbind <name>");
                    continue;
                }
                bool erased = false;
                {
                    lock_guard<mutex> lock(binds_mutex);
                    if (binds.erase(name)) erased = true;
                }
                if (erased) {
                    output_message("[SYSTEM] Bind '" + name + "' removed.");
                }
                else {
                    output_message("[ERROR] Bind '" + name + "' not found.");
                }
            }
            else if (command == "//binds") {
                lock_guard<mutex> lock(binds_mutex);
                if (binds.empty()) {
                    cout << "No binds defined.\n";
                }
                else {
                    cout << "\n=== Active Binds ===\n";
                    for (const auto& [name, bind] : binds) {
                        cout << name << " : " << bind.command;
                        if (bind.interval > 0) {
                            cout << " (every " << bind.interval << "s)";
                        }
                        else {
                            cout << " (manual only)";
                        }
                        cout << "\n";
                    }
                    cout << "====================\n";
                }
            }
            else if (command.find("//send") == 0) {
                string name = command.size() > 6 ? command.substr(7) : "";
                if (name.empty()) {
                    output_message("[ERROR] Usage: //send <name>");
                    continue;
                }
                bool found = false;
                string cmd_to_send;
                {
                    lock_guard<mutex> lock(binds_mutex);
                    auto it = binds.find(name);
                    if (it != binds.end()) {
                        cmd_to_send = it->second.command;
                        found = true;
                    }
                }
                if (!found) {
                    output_message("[ERROR] Bind '" + name + "' not found.");
                    continue;
                }
                if (!connected_to_game) {
                    output_message("[ERROR] Not connected to game server. Cannot send.");
                    continue;
                }
                if (safe_send(game_socket, cmd_to_send)) {
                    output_message("[SYSTEM] Sent command: " + cmd_to_send);
                }
                else {
                    output_message("[ERROR] Failed to send command.");
                }
            }
            // ---------- Цвета ----------
            else if (command.find("//change_colors") == 0) {
                string rest = command.size() > 16 ? command.substr(16) : "";
                size_t start = rest.find_first_not_of(" \t");
                if (start == string::npos) {
                    show_color_table();
                    continue;
                }
                rest = rest.substr(start);
                stringstream ss(rest);
                string color_name, new_code_str;
                ss >> color_name >> new_code_str;
                if (color_name.empty() || new_code_str.empty()) {
                    output_message("[ERROR] Usage: //change_colors <color_name> <new_hex_code>");
                    output_message("Example: //change_colors White 0");
                    continue;
                }

                string lower_name = color_name;
                transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                const char* color_names_lower[] = {
                    "black", "blue", "green", "cyan", "red", "purple", "brown", "light gray",
                    "dark gray", "light blue", "light green", "light cyan", "light red", "light purple",
                    "yellow", "white"
                };
                int idx = -1;
                for (int i = 0; i < 16; ++i) {
                    if (lower_name == color_names_lower[i]) {
                        idx = i;
                        break;
                    }
                }
                if (idx == -1) {
                    output_message("[ERROR] Unknown color name. Use '//change_colors' to see the list.");
                    continue;
                }

                if (new_code_str.length() != 1 || !isxdigit(new_code_str[0])) {
                    output_message("[ERROR] New code must be a single hex digit (0-9, A-F).");
                    continue;
                }
                int new_val = hex_char_to_int(new_code_str[0]);
                if (new_val == -1 || new_val > 15) {
                    output_message("[ERROR] Invalid hex code. Use 0-9, A-F.");
                    continue;
                }

                {
                    lock_guard<mutex> lock(color_remap_mutex);
                    color_remap[idx] = new_val;
                }
                output_message("[SYSTEM] Color '" + string(color_names_lower[idx]) + "' now maps to code " + new_code_str);
            }
            else if (command.find("//change_bg_colors") == 0) {
                string rest = command.size() > 16 ? command.substr(16) : "";
                size_t start = rest.find_first_not_of(" \t");
                rest = rest.substr(start);
                stringstream ss(rest);
                string color_name, new_code_str;
                ss >> color_name >> new_code_str;
                if (color_name.empty() || new_code_str.empty()) {
                    output_message("[ERROR] Usage: //change_bg_colors <color_name> <new_hex_code>");
                    output_message("Example: //change_bg_colors White 0");
                    continue;
                }

                string lower_name = color_name;
                transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                const char* color_names_lower[] = {
                    "black", "blue", "green", "cyan", "red", "purple", "brown", "light gray",
                    "dark gray", "light blue", "light green", "light cyan", "light red", "light purple",
                    "yellow", "white"
                };
                int idx = -1;
                for (int i = 0; i < 16; ++i) {
                    if (lower_name == color_names_lower[i]) {
                        idx = i;
                        break;
                    }
                }
                if (idx == -1) {
                    output_message("[ERROR] Unknown color name. Use '//change_colors' to see the list.");
                    continue;
                }

                if (new_code_str.length() != 1 || !isxdigit(new_code_str[0])) {
                    output_message("[ERROR] New code must be a single hex digit (0-9, A-F).");
                    continue;
                }
                int new_val = hex_char_to_int(new_code_str[0]);
                if (new_val == -1 || new_val > 15) {
                    output_message("[ERROR] Invalid hex code. Use 0-9, A-F.");
                    continue;
                }

                {
                    lock_guard<mutex> lock(bg_color_remap_mutex);
                    bg_color_remap[idx] = new_val;
                }
                output_message("[SYSTEM] Color '" + string(color_names_lower[idx]) + "' now maps to code " + new_code_str);
            }
            else if (command == "//reset_colors") {
                {
                    lock_guard<mutex> lock(color_remap_mutex);
                    for (int i = 0; i < 16; ++i) color_remap[i] = i;
                    for (int i = 0; i < 16; ++i) bg_color_remap[i] = i;
                }
                output_message("[SYSTEM] All color mappings reset to defaults.");
            }
            else {
                output_message("[ERROR] Unknown command. Type '//help' for available commands.");
            }
        }
        else {
            // Обычная команда на сервер
            if (!connected_to_game) {
                output_message("[ERROR] Not connected to game server!\n");
                output_message("Use //connect");
                continue;
            }
            if (!safe_send(game_socket, command)) {
                output_message("[ERROR] Failed to send command");
            }
        }
    }
}

// ---------- MAIN ----------
int main() {
    ConsoleHelper::InitConsole();
    for (int i = 0; i < 16; ++i) {
        color_remap[i] = i;
        bg_color_remap[i] = i;
    }

    ConsoleHelper::SetColor(10, 1);
    cout << "This message should be with colored text and bg color" << endl;
    ConsoleHelper::ResetColor();
    cout << "Be carefull ";
    ConsoleHelper::SetColor(4, 14);
    cout << "EPILEPTIC!";
    ConsoleHelper::ResetColor();
    cout << "For next 8 sec there would be color check" << endl;
    this_thread::sleep_for(chrono::seconds(3));
    ConsoleHelper::ResetColor();
    for (int i = 0; i < 16; ++i) {
        ConsoleHelper::SetColor(i, 15 - i);
        cout << i;
        this_thread::sleep_for(chrono::milliseconds(50));
        system("cls");
    }
    ConsoleHelper::ResetColor();

    int LPort;
    string S_Port;
    cout << "Now let's set your splitscreen port to make them able to connect (you can ignor it and press Enter)\n";
    cout << "Local server port (must be 1-65535): ";
    getline(cin, S_Port);
    if (S_Port.empty()) {
        LPort = LOCAL_PORT;
        cerr << "\nUsing default local port " << LPort << "\n";
        this_thread::sleep_for(chrono::seconds(1));
    }
    else if (!is_valid_port(S_Port, LPort)) {
        ConsoleHelper::SetColor(8);
        cerr << "\n[ERROR] Invalid local port. Using default " << LOCAL_PORT << "\n";
        LPort = LOCAL_PORT;
        ConsoleHelper::ResetColor();
    }
    LOCAL_PORT = LPort;

    int Port;
    cout << "Now it's time to set your default port for servers  (you can ignor this too)\n";
    cout << "Game server port must (be 1-65535): ";
    getline(cin, S_Port);
    if (S_Port.empty()) {
        Port = SERVER_PORT;
        cerr << "\nUsing default game port " << Port << "\n";
        this_thread::sleep_for(chrono::seconds(1));
    }
    else if (!is_valid_port(S_Port, Port)) {
        ConsoleHelper::SetColor(8);
        cerr << "\n[ERROR] Invalid server port. Using default " << SERVER_PORT << "\n";
        Port = SERVER_PORT;
        ConsoleHelper::ResetColor();
    }
    SERVER_PORT = Port;

    system("cls");
    cout << "=== MCG Multi-Window Client (only Windows) ===\n";
    cout << "Local server port: " << LOCAL_PORT << "\n";
    cout << "Game server port: " << SERVER_PORT << "\n";
    cout << "==============================================\n\n";
    ConsoleHelper::SetColor(8);
    cout << "Note: If you don't open separate windows, all information\n";
    cout << "will be displayed directly in this console.\n";
    cout << "Use '//windows' command to see how to open separate windows.\n\n";
    ConsoleHelper::ResetColor();

    if (!network_init()) {
        cerr << "\n[FATAL] Network initialization failed!\n";
        cerr << "This message means that MCG_Client couldn't initiate Network,\n        try restart client,\n          it might work\n";
        this_thread::sleep_for(chrono::seconds(5));
        return 1;
    }

    // Запускаем поток биндов
    binds_thread = thread(binds_loop);

    thread server_thread(local_server);
    input_loop();

    running = false;
    local_server_running = false;
    connected_to_game = false;
    cleanup_local_clients();
    if (game_socket != INVALID_SOCKET) closesocket(game_socket);
    if (server_thread.joinable()) server_thread.join();

    // Останавливаем поток биндов
    binds_running = false;
    if (binds_thread.joinable()) binds_thread.join();

    network_cleanup();

    cout << "\nClient terminated.\n";
    this_thread::sleep_for(chrono::seconds(5));
    return 0;
}