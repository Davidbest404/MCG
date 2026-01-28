// MCG_Client.cpp - Основной клиент + локальный сервер
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

// Кроссплатформенные заголовки
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#define SOCKET_TYPE SOCKET
#define INVALID_SOCKET_VAL INVALID_SOCKET
#define CLOSE_SOCKET closesocket
#define GET_LAST_ERROR WSAGetLastError()
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <fcntl.h>
#define SOCKET_TYPE int
#define INVALID_SOCKET_VAL -1
#define CLOSE_SOCKET close
#define GET_LAST_ERROR errno
#endif

using namespace std;

// Порт основного сервера и локального сервера
const int GAME_PORT = 8080;
const int LOCAL_PORT = 9090;

atomic<bool> running(true);
atomic<bool> connected_to_game(false);
atomic<bool> local_server_running(false);

// Типы локальных клиентов
enum class ClientType {
    CHAT_WINDOW,
    MAP_WINDOW,
    STATUS_WINDOW,
    UNKNOWN
};

// Структура для локального клиента
struct LocalClient {
    SOCKET_TYPE socket;
    ClientType type;
    string name;
};

// Глобальные данные
SOCKET_TYPE game_socket = INVALID_SOCKET_VAL;
vector<LocalClient> local_clients;
mutex local_clients_mutex;

// Функция для безопасной отправки данных
bool safe_send(SOCKET_TYPE sock, const string& data) {
    if (sock == INVALID_SOCKET_VAL) return false;

#ifdef _WIN32
    int result = send(sock, data.c_str(), static_cast<int>(data.size()), 0);
#else
    ssize_t result = send(sock, data.c_str(), data.size(), 0);
#endif
    return result > 0;
}

// Функция для безопасного приема данных
string safe_receive(SOCKET_TYPE sock, int timeout_ms = 1000) {
    if (sock == INVALID_SOCKET_VAL) return "";

    // Настраиваем timeout
#ifdef _WIN32
    DWORD timeout = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
#else
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));

#ifdef _WIN32
    int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
#else
    ssize_t bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
#endif

    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        return string(buffer);
    }

    return "";
}

// Инициализация сети
bool network_init() {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
    return true;
#endif
}

// Очистка сети
void network_cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// Определение типа сообщения
enum class MessageType {
    CHAT_MESSAGE,
    MAP_UPDATE,
    STATUS_UPDATE,
    COMMAND_RESPONSE,
    SYSTEM_MESSAGE
};

MessageType get_message_type(const string& message) {
    if (message.find("=== Game Map ===") != string::npos ||
        message.find("Position: (") != string::npos) {
        return MessageType::MAP_UPDATE;
    }
    else if (message.find("=== Your Status ===") != string::npos ||
        message.find("HP: ") != string::npos ||
        message.find("Level: ") != string::npos) {
        return MessageType::STATUS_UPDATE;
    }
    else if (message.find("[SERVER]") != string::npos ||
        message.find("attacked") != string::npos ||
        message.find("moved") != string::npos) {
        return MessageType::CHAT_MESSAGE;
    }
    else if (message.find("ERROR") != string::npos ||
        message.find("OK") != string::npos) {
        return MessageType::COMMAND_RESPONSE;
    }
    return MessageType::SYSTEM_MESSAGE;
}

// Отправка сообщения локальным клиентам
void send_to_local_clients(const string& message, ClientType target_type = ClientType::UNKNOWN) {
    lock_guard<mutex> lock(local_clients_mutex);

    for (const auto& client : local_clients) {
        if (target_type == ClientType::UNKNOWN || client.type == target_type) {
            safe_send(client.socket, message + "\n");
        }
    }
}

// Обработка сообщений от игрового сервера
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
        // Отправляем всем окнам для отображения
        send_to_local_clients("[RESPONSE] " + message, ClientType::CHAT_WINDOW);
        break;
    default:
        send_to_local_clients("[SYSTEM] " + message, ClientType::CHAT_WINDOW);
    }
}

// Получение сообщений от игрового сервера
void receive_from_game_server() {
    char buffer[4096];

    while (running && connected_to_game) {
        memset(buffer, 0, sizeof(buffer));

#ifdef _WIN32
        int bytes_received = recv(game_socket, buffer, sizeof(buffer) - 1, 0);
#else
        ssize_t bytes_received = recv(game_socket, buffer, sizeof(buffer) - 1, 0);
#endif

        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            string message(buffer);
            process_game_message(message);
        }
        else if (bytes_received == 0) {
            // Соединение закрыто
            send_to_local_clients("[SYSTEM] Disconnected from game server", ClientType::CHAT_WINDOW);
            connected_to_game = false;
            break;
        }

        // Небольшая пауза для снижения нагрузки на CPU
        this_thread::sleep_for(chrono::milliseconds(10));
    }
}

// Подключение к игровому серверу
bool connect_to_game_server(const string& ip_address) {
    game_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (game_socket == INVALID_SOCKET_VAL) {
        send_to_local_clients("[ERROR] Failed to create socket", ClientType::CHAT_WINDOW);
        return false;
    }

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(GAME_PORT);

    // Кроссплатформенное преобразование IP
    // Для простоты используем inet_addr (IPv4)
    server_addr.sin_addr.s_addr = inet_addr(ip_address.c_str());
    if (server_addr.sin_addr.s_addr == INADDR_NONE) {
        // Пробуем альтернативный метод для localhost
        if (ip_address == "localhost" || ip_address == "127.0.0.1") {
            server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        }
        else {
            send_to_local_clients("[ERROR] Invalid IP address: " + ip_address, ClientType::CHAT_WINDOW);
            CLOSE_SOCKET(game_socket);
            game_socket = INVALID_SOCKET_VAL;
            return false;
        }
    }

    send_to_local_clients("[SYSTEM] Connecting to game server at " + ip_address + "...", ClientType::CHAT_WINDOW);

    if (connect(game_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        send_to_local_clients("[ERROR] Connection failed. Make sure server is running.", ClientType::CHAT_WINDOW);
        CLOSE_SOCKET(game_socket);
        game_socket = INVALID_SOCKET_VAL;
        return false;
    }

    // Устанавливаем неблокирующий режим для таймаутов
#ifdef _WIN32
    u_long mode = 0; // 0 для блокирующего режима
    ioctlsocket(game_socket, FIONBIO, &mode);
#else
    int flags = fcntl(game_socket, F_GETFL, 0);
    fcntl(game_socket, F_SETFL, flags & ~O_NONBLOCK); // Убираем неблокирующий режим
#endif

    // Получаем приветственное сообщение
    string welcome = safe_receive(game_socket, 2000);
    if (!welcome.empty()) {
        process_game_message(welcome);
    }

    connected_to_game = true;
    send_to_local_clients("[SYSTEM] Connected to game server!", ClientType::CHAT_WINDOW);

    return true;
}

// Аутентификация на игровом сервере
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

// Локальный сервер для окон
void local_server() {
    SOCKET_TYPE server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET_VAL) {
        return;
    }

    // Разрешаем повторное использование порта
    int opt = 1;
#ifdef _WIN32
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#else
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(LOCAL_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "[ERROR] Failed to bind local server to port " << LOCAL_PORT << endl;
        CLOSE_SOCKET(server_socket);
        return;
    }

    if (listen(server_socket, 5) < 0) {
        cerr << "[ERROR] Failed to listen on port " << LOCAL_PORT << endl;
        CLOSE_SOCKET(server_socket);
        return;
    }

    cout << "[SYSTEM] Local server started on port " << LOCAL_PORT << endl;
    local_server_running = true;

    while (running && local_server_running) {
        sockaddr_in client_addr;
#ifdef _WIN32
        int addr_len = sizeof(client_addr);
#else
        socklen_t addr_len = sizeof(client_addr);
#endif

        SOCKET_TYPE client_socket = accept(server_socket, (sockaddr*)&client_addr, &addr_len);

        if (client_socket == INVALID_SOCKET_VAL) {
            continue;
        }

        // Получаем тип клиента
        string client_info = safe_receive(client_socket, 1000);

        LocalClient new_client;
        new_client.socket = client_socket;

        if (client_info.find("CHAT_WINDOW") != string::npos) {
            new_client.type = ClientType::CHAT_WINDOW;
            new_client.name = "Chat Window";
        }
        else if (client_info.find("MAP_WINDOW") != string::npos) {
            new_client.type = ClientType::MAP_WINDOW;
            new_client.name = "Map Window";
        }
        else if (client_info.find("STATUS_WINDOW") != string::npos) {
            new_client.type = ClientType::STATUS_WINDOW;
            new_client.name = "Status Window";
        }
        else {
            new_client.type = ClientType::UNKNOWN;
            new_client.name = "Unknown Window";
            safe_send(client_socket, "[ERROR] Unknown window type. Use: CHAT_WINDOW, MAP_WINDOW, or STATUS_WINDOW\n");
            CLOSE_SOCKET(client_socket);
            continue;
        }

        {
            lock_guard<mutex> lock(local_clients_mutex);
            local_clients.push_back(new_client);
        }

        cout << "[SYSTEM] " << new_client.name << " connected" << endl;

        // Отправляем приветственное сообщение
        string welcome = "=== Connected to MCG Client ===\nWindow: " + new_client.name + "\n";
        welcome += "You will receive relevant updates here.\n";
        welcome += "==================================\n\n";
        safe_send(client_socket, welcome);
    }

    CLOSE_SOCKET(server_socket);
}

// Очистка локальных клиентов
void cleanup_local_clients() {
    lock_guard<mutex> lock(local_clients_mutex);

    for (auto& client : local_clients) {
        if (client.socket != INVALID_SOCKET_VAL) {
            CLOSE_SOCKET(client.socket);
        }
    }
    local_clients.clear();
}

// Основной цикл ввода команд
void command_input_loop() {
    string input;

    cout << "Type 'help' for commands or 'windows' for window setup instructions.\n";

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
                cout << "[ERROR] Already connected to game server. Disconnect first.\n";
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

            cout << "Password: ";
            getline(cin, password);

            if (connect_to_game_server(ip)) {
                if (authenticate_on_game_server(username, password)) {
                    // Запускаем поток для получения сообщений
                    thread receive_thread(receive_from_game_server);
                    receive_thread.detach();
                }
                else {
                    // Если аутентификация не удалась, закрываем соединение
                    CLOSE_SOCKET(game_socket);
                    game_socket = INVALID_SOCKET_VAL;
                }
            }
        }
        else if (input == "disconnect") {
            if (connected_to_game) {
                CLOSE_SOCKET(game_socket);
                game_socket = INVALID_SOCKET_VAL;
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

#ifdef _WIN32
            cout << "1. Open Command Prompt or PowerShell\n";
            cout << "2. Navigate to this folder\n";
            cout << "3. Run the appropriate window client:\n";
            cout << "   - Chat Window:    chat_window.exe\n";
            cout << "   - Map Window:     map_window.exe\n";
            cout << "   - Status Window:  status_window.exe\n\n";

            cout << "Alternative: Use telnet (enable in Windows Features):\n";
            cout << "   telnet localhost " << LOCAL_PORT << "\n";
            cout << "   Then type: CHAT_WINDOW, MAP_WINDOW, or STATUS_WINDOW\n";
#else
            cout << "1. Open terminal\n";
            cout << "2. Navigate to this folder\n";
            cout << "3. Run the appropriate window client:\n";
            cout << "   - Chat Window:    ./chat_window\n";
            cout << "   - Map Window:     ./map_window\n";
            cout << "   - Status Window:  ./status_window\n\n";

            cout << "Alternative: Use netcat or telnet:\n";
            cout << "   echo 'CHAT_WINDOW' | nc localhost " << LOCAL_PORT << "\n";
            cout << "   or telnet localhost " << LOCAL_PORT << "\n";
#endif

            cout << "\nNote: Windows will automatically receive relevant updates.\n";
            cout << "========================================\n\n";
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
                // ТОЛЬКО здесь отправляем уведомление в окно чата
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

            string status_cmd = input;
            if (safe_send(game_socket, status_cmd)) {
                cout << "[INFO] Status request sent\n";
                // НЕ отправляем в окно чата - только в консоль клиента
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
                // НЕ отправляем в окно чата - только в консоль клиента
            }
            else {
                cout << "[ERROR] Failed to send map request\n";
            }
        }
        else {
            cout << "[ERROR] Unknown command. Type 'help' for available commands.\n";
            // НЕ отправляем в окно чата
        }
    }
}

// Главная функция
int main() {
    cout << "=== MCG Multi-Window Client ===\n";
    cout << "Local server port: " << LOCAL_PORT << "\n";
    cout << "Game server port: " << GAME_PORT << "\n";
    cout << "===============================\n\n";

    if (!network_init()) {
        cerr << "[FATAL] Network initialization failed!\n";
        return 1;
    }

    // Запускаем локальный сервер в отдельном потоке
    thread server_thread(local_server);

    // Основной цикл ввода команд
    command_input_loop();

    // Очистка
    running = false;
    local_server_running = false;
    connected_to_game = false;

    // Закрываем все соединения
    cleanup_local_clients();

    if (game_socket != INVALID_SOCKET_VAL) {
        CLOSE_SOCKET(game_socket);
    }

    if (server_thread.joinable()) {
        server_thread.join();
    }

    network_cleanup();

    cout << "\nClient terminated.\n";
    return 0;
}