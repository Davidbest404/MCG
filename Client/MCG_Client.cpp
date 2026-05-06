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
//Для большего количества символов и цветов
#include <windows.h>
#include <io.h>
#include <fcntl.h>
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

// Глобальные счетчики открытых окон
atomic<int> chat_windows_count(0);
atomic<int> map_windows_count(0);
atomic<int> status_windows_count(0);

class ConsoleHelper {
public:
    // Инициализация консоли для поддержки Unicode и русского
    static void InitConsole() {
#ifdef _WIN32
        // Устанавливаем кодовую страницу UTF-8
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);

        // Настраиваем буфер для поддержки Unicode
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode = 0;
        GetConsoleMode(hOut, &dwMode);
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING; // Для ANSI escape-кодов
        SetConsoleMode(hOut, dwMode);

        // Для старых версий Windows (до Win10)
        // используем SetConsoleFont
        SetConsoleFont();
#endif
    }

    // Установка шрифта, поддерживающего Unicode
    static void SetConsoleFont() {
#ifdef _WIN32
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_FONT_INFOEX fontInfo;
        fontInfo.cbSize = sizeof(fontInfo);
        GetCurrentConsoleFontEx(hConsole, FALSE, &fontInfo);

        // Шрифт Consolas хорошо поддерживает Unicode
        wcscpy_s(fontInfo.FaceName, L"Consolas");
        SetCurrentConsoleFontEx(hConsole, FALSE, &fontInfo);
#endif
    }

    // Установка цвета текста
    static void SetColor(int textColor, int bgColor = 0) {
#ifdef _WIN32
        HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hStdOut, (WORD)((bgColor << 4) | textColor));
#else
        // ANSI color codes для Linux/Mac
        cout << "\033[" << (30 + textColor) << "m";
#endif
    }

    // Сброс цвета
    static void ResetColor() {
#ifdef _WIN32
        SetColor(7);
#else
        cout << "\033[0m";
#endif
    }
};

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
        message.find("Name: ") != string::npos ||
        message.find("HP: ") != string::npos) {
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

// Функция для вывода в основное окно с цветом
void print_to_main_console(const string& message, int color = 7, const string& prefix = "") {
    ConsoleHelper::SetColor(color);
    if (!prefix.empty()) {
        cout << prefix;
    }
    cout << message;
    ConsoleHelper::ResetColor();
}

// Отправка сообщения локальным клиентам (с fallback в основное окно)
void send_to_local_clients(const string& message, ClientType target_type = ClientType::UNKNOWN) {
    bool sent = false;

    {
        lock_guard<mutex> lock(local_clients_mutex);

        for (const auto& client : local_clients) {
            if (target_type == ClientType::UNKNOWN || client.type == target_type) {
                if (safe_send(client.socket, message + "\n")) {
                    sent = true;
                }
            }
        }
    }

    // Если нет открытых окон нужного типа, выводим в основное окно
    if (!sent) {
        // Определяем цвет и префикс в зависимости от типа сообщения
        int color = 7; // белый по умолчанию
        string prefix = "";

        if (target_type == ClientType::CHAT_WINDOW) {
            color = 10; // зеленый для чата
            prefix = "[CHAT] ";
        }
        else if (target_type == ClientType::MAP_WINDOW) {
            color = 14; // желтый для карты
            prefix = "[MAP]\n";
        }
        else if (target_type == ClientType::STATUS_WINDOW) {
            color = 11; // голубой для статуса
            prefix = "[STATUS]\n";
        }

        // Убираем префиксы из сообщения, если они там есть
        string clean_message = message;
        if (clean_message.find("[CHAT] ") == 0) clean_message = clean_message.substr(7);
        else if (clean_message.find("[MAP]\n") == 0) clean_message = clean_message.substr(6);
        else if (clean_message.find("[STATUS]\n") == 0) clean_message = clean_message.substr(9);

        print_to_main_console(prefix + clean_message, color);
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
            print_to_main_console("[SYSTEM] Disconnected from game server\n", 6);
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
        print_to_main_console("[ERROR] Failed to create socket\n", 6);
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
            print_to_main_console("[ERROR] Invalid IP address: " + ip_address + "\n", 4);
            CLOSE_SOCKET(game_socket);
            game_socket = INVALID_SOCKET_VAL;
            return false;
        }
    }

    print_to_main_console("[SYSTEM] Connecting to game server at " + ip_address + "...\n", 10);

    if (connect(game_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        print_to_main_console("[ERROR] Connection failed. Make sure server is running.\n", 4);
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
    send_to_local_clients("[SYSTEM] Connected to game server!\n", ClientType::CHAT_WINDOW);

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
            chat_windows_count++;
        }
        else if (client_info.find("MAP_WINDOW") != string::npos) {
            new_client.type = ClientType::MAP_WINDOW;
            new_client.name = "Map Window";
            map_windows_count++;
        }
        else if (client_info.find("STATUS_WINDOW") != string::npos) {
            new_client.type = ClientType::STATUS_WINDOW;
            new_client.name = "Status Window";
            status_windows_count++;
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
            if (client.type == ClientType::CHAT_WINDOW) chat_windows_count--;
            else if (client.type == ClientType::MAP_WINDOW) map_windows_count--;
            else if (client.type == ClientType::STATUS_WINDOW) status_windows_count--;
            CLOSE_SOCKET(client.socket);
        }
    }
    local_clients.clear();
}

// Основной цикл ввода команд
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

            // Показываем статус открытых окон
            cout << "\n=== Connected Windows Status ===\n";
            cout << "Chat Windows:   " << chat_windows_count << " (messages will go here)\n";
            cout << "Map Windows:    " << map_windows_count << " (map updates will go here)\n";
            cout << "Status Windows: " << status_windows_count << " (status updates will go here)\n";
            cout << "================================\n\n";

            if (chat_windows_count == 0) {
                print_to_main_console("Note: No chat windows open. Chat messages will appear in this console.\n", 14);
            }
            if (map_windows_count == 0) {
                print_to_main_console("Note: No map windows open. Map updates will appear in this console.\n", 14);
            }
            if (status_windows_count == 0) {
                print_to_main_console("Note: No status windows open. Status updates will appear in this console.\n", 14);
            }
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
    ConsoleHelper::InitConsole();

    cout << "=== MCG Multi-Window Client ===\n";
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