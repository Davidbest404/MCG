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

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

int SERVER_PORT = 8080;
int LOCAL_PORT = 9090;

atomic<bool> running(true);
atomic<bool> connected_to_game(false);
atomic<bool> local_server_running(false);

atomic<int> chat_windows_count(0);
atomic<int> map_windows_count(0);
atomic<int> status_windows_count(0);

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

    static void ResetColor() {
        SetColor(7);
    }
};

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

bool safe_send(SOCKET sock, const string& data) {
    if (sock == INVALID_SOCKET) return false;
    int result = send(sock, data.c_str(), static_cast<int>(data.size()), 0);
    return result > 0;
}

string safe_receive(SOCKET sock, int timeout_ms = 1000) {
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

bool network_init() {
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

void network_cleanup() {
    WSACleanup();
}

enum class MessageType {
    CHAT_MESSAGE,
    MAP_UPDATE,
    STATUS_UPDATE,
    COMMAND_RESPONSE,
    ERROR_RESPONSE,
    SYSTEM_MESSAGE
};


MessageType get_message_type(const string& message) {
    if (message.find("[MAP]") != string::npos)
        return MessageType::MAP_UPDATE;
    else if (message.find("[STATUS]") != string::npos)
        return MessageType::STATUS_UPDATE;
    else if (message.find("[SERVER]") != string::npos)
        return MessageType::CHAT_MESSAGE;
    else if (message.find("[COMMAND]") != string::npos)
        return MessageType::COMMAND_RESPONSE;
    else if (message.find("[ERROR]") != string::npos)
        return MessageType::COMMAND_RESPONSE;
    return MessageType::SYSTEM_MESSAGE;
}

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
int hex_char_to_int(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

void print_colored_text(const string& text) {
    vector<int> text_stack = { 7 };  // текущий цвет текста
    vector<int> bg_stack = { 0 };    // текущий цвет фона
    size_t i = 0;
    size_t len = text.length();

    auto apply = [&]() {
        ConsoleHelper::SetColor(text_stack.back(), bg_stack.back());
        };

    while (i < len) {
        if (text[i] == '[' && i + 3 < len && text[i + 1] == 'c' && isxdigit(text[i + 2]) && text[i + 3] == ']') {
            // Открывающий тег [cX]
            int color = hex_char_to_int(text[i + 2]);
            if (color != -1) {
                text_stack.push_back(color);
                apply();
                i += 4;
                continue;
            }
        }
        else if (text[i] == '[' && i + 4 < len && text[i + 1] == '/' && text[i + 2] == 'c' && isxdigit(text[i + 3]) && text[i + 4] == ']') {
            // Закрывающий тег [/cX]
            if (text_stack.size() > 1) {
                text_stack.pop_back();
                apply();
            }
            i += 5;
            continue;
        }
        else if (text[i] == '[' && i + 4 < len && text[i + 1] == 'b' && text[i + 2] == 'g' && isxdigit(text[i + 3]) && text[i + 4] == ']') {
            // Открывающий тег [bgX]
            int color = hex_char_to_int(text[i + 3]);
            if (color != -1) {
                bg_stack.push_back(color);
                apply();
                i += 5;
                continue;
            }
        }
        else if (text[i] == '[' && i + 5 < len && text[i + 1] == '/' && text[i + 2] == 'b' && text[i + 3] == 'g' && isxdigit(text[i + 4]) && text[i + 5] == ']') {
            // Закрывающий тег [/bgX]
            if (bg_stack.size() > 1) {
                bg_stack.pop_back();
                apply();
            }
            i += 6;
            continue;
        }

        // Обычный символ
        apply();
        cout << text[i];
        i++;
    }
    ConsoleHelper::ResetColor(); // сброс в консоли после всей строки
}

void print_to_main_console(const string& message, int color = 7, const string& prefix = "") {
    ConsoleHelper::SetColor(color);
    if (!prefix.empty()) cout << prefix;
    cout << message;
    ConsoleHelper::ResetColor();
}

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
        if (target_type == ClientType::CHAT_WINDOW) {
            cout << "[CHAT] ";
            print_colored_text(message);
        }
        else if (target_type == ClientType::MAP_WINDOW) {
            cout << "[MAP]\n";
            print_colored_text(message);
        }
        else if (target_type == ClientType::STATUS_WINDOW) {
            cout << "[STATUS]\n";
            print_colored_text(message);
        }
        else {
            print_colored_text(message);
        }
        ConsoleHelper::ResetColor(); // <--- Сброс цвета после вывода
        if (!message.empty() && message.back() != '\n') cout << '\n';
    }
}

void process_game_message(const string& message) {
    if (message.empty()) return;
    MessageType type = get_message_type(message);
    switch (type) {
    case MessageType::CHAT_MESSAGE:
        send_to_local_clients(message, ClientType::CHAT_WINDOW);
        break;
    case MessageType::MAP_UPDATE:
        send_to_local_clients(message, ClientType::MAP_WINDOW);
        break;
    case MessageType::STATUS_UPDATE:
        send_to_local_clients(message, ClientType::STATUS_WINDOW);
        break;
    default:
        send_to_local_clients(message, ClientType::CHAT_WINDOW);
        break;
    }
}

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
            print_to_main_console("[SYSTEM] Disconnected from game server\n", 6);
            connected_to_game = false;
            break;
        }
        this_thread::sleep_for(chrono::milliseconds(10));
    }
}

bool connect_to_game_server(const string& ip_address, int& port) {
    game_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (game_socket == INVALID_SOCKET) {
        print_to_main_console("[ERROR] Failed to create socket\n", 6);
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
            print_to_main_console("[ERROR] Invalid IP address or port: " + ip_address + to_string(port) + "\n", 4);
            closesocket(game_socket);
            game_socket = INVALID_SOCKET;
            return false;
        }
    }
    print_to_main_console("[SYSTEM] Connecting to game server at " + ip_address + ":" + to_string(port) + "...\n", 10);
    if (connect(game_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        print_to_main_console("[ERROR] Connection failed. Make sure server is running.\n", 4);
        closesocket(game_socket);
        game_socket = INVALID_SOCKET;
        return false;
    }
    u_long mode = 0;
    ioctlsocket(game_socket, FIONBIO, &mode);
    string welcome = safe_receive(game_socket, 2000);
    if (!welcome.empty()) process_game_message(welcome);
    connected_to_game = true;
    send_to_local_clients("[SYSTEM] Connected to game server!\n", ClientType::CHAT_WINDOW);
    return true;
}

/*
bool authenticate_on_game_server(const string& username, const string& password) {
    string auth_msg = "AUTH|" + username + "|" + password;
    if (!safe_send(game_socket, auth_msg)) {
        send_to_local_clients("[ERROR] Authentication failed", ClientType::CHAT_WINDOW);
        return false;
    }
    string response = safe_receive(game_socket, 3000);
    if (response.empty() || response.find("OK") == string::npos) {
        send_to_local_clients("[ERROR] Authentication failed: " + response, ClientType::CHAT_WINDOW);
        return false;
    }
    process_game_message(response);
    return true;
}
*/

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
    cout << "\n[SYSTEM] Local server started on port " << LOCAL_PORT << endl;
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

bool is_valid_ip(const string& ip) {
    if (ip == "localhost" || ip == "127.0.0.1") return true;
    // Попробуем преобразовать строку в IPv4-адрес
    struct in_addr addr;
    int result = inet_pton(AF_INET, ip.c_str(), &addr);
    return result == 1;
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

void command_input_loop() {
    string input;
    print_to_main_console("Type 'help' for commands or 'windows' for window setup instructions.\n", 6);
    while (running) {
        ConsoleHelper::ResetColor();
        getline(cin, input);
        if (input.empty()) continue;
        if (input == "exit" || input == "quit") {
            running = false;
            break;
        }
        else if (input == "connect") {
            if (connected_to_game) {
                print_to_main_console("\n[ERROR] Already connected to game server. Disconnect first.\n", 6);
                continue;
            }
            string ip, username, password;
            int port;
            cout << "Server IP [127.0.0.1]: ";
            getline(cin, ip);
            if (ip.empty()) ip = "127.0.0.1";
            // Проверка IP
            if (!is_valid_ip(ip)) {
                print_to_main_console("\n[ERROR] Invalid IP address format.\n", 4);
                continue;
            }

            cout << "\nServer port [" << SERVER_PORT << "]: ";
            string portStr;
            getline(cin, portStr);
            if (portStr.empty()) {
                port = SERVER_PORT;
            }
            else {
                if (!is_valid_port(portStr, port)) {
                    print_to_main_console("\n[ERROR] Invalid port number (must be 1-65535).\n", 4);
                    continue;
                }
            }

            getline(cin, password);
            if (connect_to_game_server(ip, port)) {
                string response = safe_receive(game_socket, 3000);
                if (response.empty() || response.find("OK") == string::npos) {
                    thread receive_thread(receive_from_game_server);
                    receive_thread.detach();
                    process_game_message(response);
                }
                else {
                    closesocket(game_socket);
                    game_socket = INVALID_SOCKET;
                }
            }
        }
        else if (input == "disconnect") {
            if (connected_to_game) {
                closesocket(game_socket);
                game_socket = INVALID_SOCKET;
                connected_to_game = false;
                send_to_local_clients("[SYSTEM] Disconnected from game server", ClientType::CHAT_WINDOW);
                cout << "\n[SYSTEM] Disconnected from game server\n";
            }
            else {
                cout << "\n[ERROR] Not connected to game server\n";
            }
        }
        else if (input == "windows") {
            cout << "\n=== Window Connection Instructions ===\n";
            cout << "Local server is running on port " << LOCAL_PORT << "\n\n";
            cout << "For each window, open a NEW terminal and run:\n";
            cout << "---------------------------------------------\n";
            cout << "1. Open Command Prompt or PowerShell\n";
            cout << "2. Navigate to this folder\n";
            cout << "3. Run the appropriate window client:\n";
            cout << "   - Chat Window:    chat_window.exe\n";
            cout << "   - Map Window:     map_window.exe\n";
            cout << "   - Status Window:  status_window.exe\n\n";
            cout << "Alternative: Use telnet (enable in Windows Features):\n";
            cout << "   telnet localhost " << LOCAL_PORT << "\n";
            cout << "   Then type: CHAT_WINDOW, MAP_WINDOW, or STATUS_WINDOW\n";
            cout << "\nNote: Windows will automatically receive relevant updates.\n";
            cout << "========================================\n\n";
            cout << "\n=== Connected Windows Status ===\n";
            cout << "Chat Windows:   " << chat_windows_count << " (messages will go here)\n";
            cout << "Map Windows:    " << map_windows_count << " (map updates will go here)\n";
            cout << "Status Windows: " << status_windows_count << " (status updates will go here)\n";
            cout << "================================\n\n";
            if (chat_windows_count == 0)
                print_to_main_console("Note: No chat windows open. Chat messages will appear in this console.\n", 14);
            if (map_windows_count == 0)
                print_to_main_console("Note: No map windows open. Map updates will appear in this console.\n", 14);
            if (status_windows_count == 0)
                print_to_main_console("Note: No status windows open. Status updates will appear in this console.\n", 14);
        }
        else if (input == "help") {
            cout << "\n=== MCG Client Commands ===\n";
            cout << "connect     - Connect to game server\n";
            cout << "disconnect  - Disconnect from game server\n";
            cout << "windows     - Show window connection instructions\n";
            cout << "send [cmd]  - Send command to game server\n";
            cout << "            Example: send /help (server commands in chat)\n";
            cout << "            Example: send Hello everyone! (chat message)\n";
            cout << "status [id] - Get player status (with optional ID)\n";
            cout << "map         - Get game map\n";
            cout << "clients     - Show connected windows\n";
            cout << "help        - Show this help\n";
            cout << "exit        - Exit client\n";
            cout << "=============================\n";
        }
        else if (input == "clients") {
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
        else if (input.find("send ") == 0) {
            if (!connected_to_game) {
                cout << "\n[ERROR] Not connected to game server!\n";
                continue;
            }
            string command = input.substr(5);
            if (command.empty()) {
                cout << "\n[ERROR] No command specified\n";
                continue;
            }
            if (safe_send(game_socket, command)) {
            }
            else {
                cout << "\n[ERROR] Failed to send command\n";
            }
        }
        else if (input.find("status") == 0) {
            if (!connected_to_game) {
                cout << "\n[ERROR] Not connected to game server!\n";
                continue;
            }
            if (safe_send(game_socket, input)) {
            }
            else {
                cout << "\n[ERROR] Failed to send status request\n";
            }
        }
        else if (input == "map") {
            if (!connected_to_game) {
                cout << "\n[ERROR] Not connected to game server!\n";
                continue;
            }
            if (safe_send(game_socket, "/map")) {
            }
            else {
                cout << "\n[ERROR] Failed to send map request\n";
            }
        }
        else {
            cout << "\n[ERROR] Unknown command. Type 'help' for available commands.\n";
        }
    }
}

int main() {
    ConsoleHelper::InitConsole();
    ConsoleHelper::SetColor(15, 1); // белый текст на синем фоне
    cout << "[DEBUG] " << endl;
    ConsoleHelper::ResetColor();
    //------------
    int LPort;
    string S_LPort;
    getline(cin, S_LPort);
    if (S_LPort.empty()) {
        LPort = LOCAL_PORT;
    }
    else {
        if (!is_valid_port(S_LPort, LPort)) {
            cerr << "\n[ERROR] Invalid local port. Using default " << LOCAL_PORT << "\n";
            LPort = LOCAL_PORT;
        }
    }
    LOCAL_PORT = LPort;

    int Port;
    string S_Port;
    getline(cin, S_Port);
    if (S_Port.empty()) {
        Port = SERVER_PORT;
    }
    else {
        if (!is_valid_port(S_Port, Port)) {
            cerr << "\n[ERROR] Invalid server port. Using default " << SERVER_PORT << "\n";
            Port = SERVER_PORT;
        }
    }
    SERVER_PORT = Port;
    //------------
    system("cls");
    cout << "=== MCG Multi-Window Client (Windows) ===\n";
    cout << "Local server port: " << LOCAL_PORT << "\n";
    cout << "Game server port: " << SERVER_PORT << "\n";
    cout << "===============================\n\n";
    cout << "Note: If you don't open separate windows, all information\n";
    cout << "will be displayed directly in this console.\n";
    cout << "Use 'windows' command to see how to open separate windows.\n\n";
    if (!network_init()) {
        cerr << "\n[FATAL] Network initialization failed!\n";
        return 1;
    }
    thread server_thread(local_server);
    command_input_loop();
    running = false;
    local_server_running = false;
    connected_to_game = false;
    cleanup_local_clients();
    if (game_socket != INVALID_SOCKET) closesocket(game_socket);
    if (server_thread.joinable()) server_thread.join();
    network_cleanup();
    cout << "\nClient terminated.\n";
    return 0;
}