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

const int GAME_PORT = 8080;
const int LOCAL_PORT = 9090;

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
    SYSTEM_MESSAGE
};

MessageType get_message_type(const string& message) {
    if (message.find("=== Game Map ===") != string::npos || message.find("Position: (") != string::npos)
        return MessageType::MAP_UPDATE;
    else if (message.find("=== Your Status ===") != string::npos || message.find("Name: ") != string::npos || message.find("HP: ") != string::npos)
        return MessageType::STATUS_UPDATE;
    else if (message.find("[SERVER]") != string::npos || message.find("attacked") != string::npos || message.find("moved") != string::npos)
        return MessageType::CHAT_MESSAGE;
    else if (message.find("ERROR") != string::npos || message.find("OK") != string::npos)
        return MessageType::COMMAND_RESPONSE;
    return MessageType::SYSTEM_MESSAGE;
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
        int color = 7;
        string prefix = "";
        if (target_type == ClientType::CHAT_WINDOW) { color = 10; prefix = "[CHAT] "; }
        else if (target_type == ClientType::MAP_WINDOW) { color = 14; prefix = "[MAP]\n"; }
        else if (target_type == ClientType::STATUS_WINDOW) { color = 11; prefix = "[STATUS]\n"; }
        string clean_message = message;
        if (clean_message.find("[CHAT] ") == 0) clean_message = clean_message.substr(7);
        else if (clean_message.find("[MAP]\n") == 0) clean_message = clean_message.substr(6);
        else if (clean_message.find("[STATUS]\n") == 0) clean_message = clean_message.substr(9);
        print_to_main_console(prefix + clean_message, color);
    }
}

void process_game_message(const string& message) {
    if (message.empty()) return;
    MessageType type = get_message_type(message);
    switch (type) {
    case MessageType::CHAT_MESSAGE:
        send_to_local_clients("[CHAT] " + message, ClientType::CHAT_WINDOW);
        break;
    case MessageType::MAP_UPDATE:
        send_to_local_clients("[MAP]\n" + message, ClientType::MAP_WINDOW);
        break;
    case MessageType::STATUS_UPDATE:
        send_to_local_clients("[STATUS]\n" + message, ClientType::STATUS_WINDOW);
        break;
    case MessageType::COMMAND_RESPONSE:
        send_to_local_clients("[RESPONSE] " + message, ClientType::CHAT_WINDOW);
        break;
    default:
        send_to_local_clients("[SYSTEM] " + message, ClientType::CHAT_WINDOW);
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

bool connect_to_game_server(const string& ip_address) {
    game_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (game_socket == INVALID_SOCKET) {
        print_to_main_console("[ERROR] Failed to create socket\n", 6);
        return false;
    }
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(GAME_PORT);
    server_addr.sin_addr.s_addr = inet_addr(ip_address.c_str());
    if (server_addr.sin_addr.s_addr == INADDR_NONE) {
        if (ip_address == "localhost" || ip_address == "127.0.0.1") {
            server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        }
        else {
            print_to_main_console("[ERROR] Invalid IP address: " + ip_address + "\n", 4);
            closesocket(game_socket);
            game_socket = INVALID_SOCKET;
            return false;
        }
    }
    print_to_main_console("[SYSTEM] Connecting to game server at " + ip_address + "...\n", 10);
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
        cerr << "[ERROR] Failed to bind local server to port " << LOCAL_PORT << endl;
        closesocket(server_socket);
        return;
    }
    if (listen(server_socket, 5) < 0) {
        cerr << "[ERROR] Failed to listen on port " << LOCAL_PORT << endl;
        closesocket(server_socket);
        return;
    }
    cout << "[SYSTEM] Local server started on port " << LOCAL_PORT << endl;
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
        cout << "[SYSTEM] " << new_client.name << " connected" << endl;
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

void command_input_loop() {
    string input;
    print_to_main_console("Type 'help' for commands or 'windows' for window setup instructions.\n", 6);
    while (running) {
        cout << "MCG Client> ";
        getline(cin, input);
        if (input.empty()) continue;
        if (input == "exit" || input == "quit") {
            running = false;
            break;
        }
        else if (input == "connect") {
            if (connected_to_game) {
                print_to_main_console("[ERROR] Already connected to game server. Disconnect first.\n", 6);
                continue;
            }
            string ip, username, password;
            cout << "Server IP [127.0.0.1]: ";
            getline(cin, ip);
            if (ip.empty()) ip = "127.0.0.1";
            cout << "Username: ";
            getline(cin, username);
            if (username.empty()) {
                cout << "[ERROR] Username cannot be empty\n";
                continue;
            }
            cout << "Config password: ";
            getline(cin, password);
            if (connect_to_game_server(ip)) {
                if (authenticate_on_game_server(username, password)) {
                    thread receive_thread(receive_from_game_server);
                    receive_thread.detach();
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
                cout << "[SYSTEM] Disconnected from game server\n";
            }
            else {
                cout << "[ERROR] Not connected to game server\n";
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
                cout << "[ERROR] Not connected to game server!\n";
                continue;
            }
            string command = input.substr(5);
            if (command.empty()) {
                cout << "[ERROR] No command specified\n";
                continue;
            }
            if (safe_send(game_socket, command)) {
                cout << "[INFO] Command sent: " << command << endl;
                send_to_local_clients("[COMMAND] Sent: " + command, ClientType::CHAT_WINDOW);
            }
            else {
                cout << "[ERROR] Failed to send command\n";
            }
        }
        else if (input.find("status") == 0) {
            if (!connected_to_game) {
                cout << "[ERROR] Not connected to game server!\n";
                continue;
            }
            if (safe_send(game_socket, input)) {
                cout << "[INFO] Status request sent\n";
            }
            else {
                cout << "[ERROR] Failed to send status request\n";
            }
        }
        else if (input == "map") {
            if (!connected_to_game) {
                cout << "[ERROR] Not connected to game server!\n";
                continue;
            }
            if (safe_send(game_socket, "/map")) {
                cout << "[INFO] Map request sent\n";
            }
            else {
                cout << "[ERROR] Failed to send map request\n";
            }
        }
        else {
            cout << "[ERROR] Unknown command. Type 'help' for available commands.\n";
        }
    }
}

int main() {
    ConsoleHelper::InitConsole();
    cout << "=== MCG Multi-Window Client (Windows) ===\n";
    cout << "Local server port: " << LOCAL_PORT << "\n";
    cout << "Game server port: " << GAME_PORT << "\n";
    cout << "===============================\n\n";
    cout << "Note: If you don't open separate windows, all information\n";
    cout << "will be displayed directly in this console.\n";
    cout << "Use 'windows' command to see how to open separate windows.\n\n";
    if (!network_init()) {
        cerr << "[FATAL] Network initialization failed!\n";
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