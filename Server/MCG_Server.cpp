#include <iostream>
#include <cstring>
#include <thread>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <atomic>

// Определяем платформу
#ifdef _WIN32
#define PLATFORM_WINDOWS 1
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

// Windows-specific
#define SHUT_RDWR SD_BOTH
#ifndef SOMAXCONN
#define SOMAXCONN 0x7fffffff
#endif
#else
#define PLATFORM_WINDOWS 0
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/types.h>

// POSIX constants
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

// Кроссплатформенные типы
#ifdef _WIN32
typedef SOCKET socket_t;
#define socket_close closesocket
#define socket_errno WSAGetLastError()
#else
typedef int socket_t;
#define socket_close close
#define socket_errno errno
#endif

using namespace std;

const int PORT = 8080;

// Глобальные данные
vector<socket_t> clients;
atomic<int> client_count(0);
map<socket_t, pair<string, int>> client_info;
atomic<int> next_client_id(1);
string Password = "null";

// Инициализация сети
bool network_init() {
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        cerr << "WSAStartup failed" << endl;
        return false;
    }
#endif
    return true;
}

// Очистка сети
void network_cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// Создание сокета
socket_t socket_create() {
    return ::socket(AF_INET, SOCK_STREAM, 0);
}

// Привязка сокета к адресу
int socket_bind(socket_t sock, const struct sockaddr* addr, socklen_t addrlen) {
    return ::bind(sock, addr, addrlen);
}

// Начало прослушивания
int socket_listen(socket_t sock, int backlog) {
    return ::listen(sock, backlog);
}

// Принятие соединения
socket_t socket_accept(socket_t sock, struct sockaddr* addr, socklen_t* addrlen) {
    return ::accept(sock, addr, addrlen);
}

// Отправка данных
int socket_send(socket_t sock, const char* data, size_t length) {
#ifdef _WIN32
    return send(sock, data, static_cast<int>(length), 0);
#else
    return send(sock, data, length, 0);
#endif
}

// Получение данных
int socket_recv(socket_t sock, char* buffer, size_t buffer_size) {
#ifdef _WIN32
    return recv(sock, buffer, static_cast<int>(buffer_size), 0);
#else
    return recv(sock, buffer, buffer_size, 0);
#endif
}

// Установка опций сокета
int socket_setopt(socket_t sock, int level, int optname, const void* optval, socklen_t optlen) {
#ifdef _WIN32
    return setsockopt(sock, level, optname, reinterpret_cast<const char*>(optval), optlen);
#else
    return setsockopt(sock, level, optname, optval, optlen);
#endif
}

// Рассылка сообщения всем клиентам кроме отправителя
void broadcast_message(const string& message, socket_t sender) {
    for (auto client : clients) {
        if (client != sender) {
            socket_send(client, message.c_str(), message.length());
        }
    }
}

// Обработчик клиента
void handle_client(socket_t client_sock) {
    char buffer[1024];

    cout << "Client connected: socket " << client_sock << endl;

    // Аутентификация
    bool authenticated = false;
    string username;

    while (!authenticated) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = socket_recv(client_sock, buffer, sizeof(buffer) - 1);

        if (bytes <= 0) {
            cout << "Client disconnected during auth" << endl;
            socket_close(client_sock);
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
                // Игнорируем пароль

                // Регистрируем пользователя
                int client_id = next_client_id++;
                client_info[client_sock] = make_pair(username, client_id);
                authenticated = true;

                string welcome = "OK|Welcome " + username + "! Your ID: " + to_string(client_id);
                socket_send(client_sock, welcome.c_str(), welcome.length());

                cout << "User '" << username << "' (ID: " << client_id << ") authenticated" << endl;

                // Уведомляем других
                string join_msg = "[SERVER] " + username + " joined the chat";
                broadcast_message(join_msg, client_sock);
            }
        }

        if (!authenticated) {
            string error = "ERROR|Use: AUTH|username|password";
            socket_send(client_sock, error.c_str(), error.length());
        }
    }

    // Основной цикл
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = socket_recv(client_sock, buffer, sizeof(buffer) - 1);

        if (bytes <= 0) {
            break;
        }

        buffer[bytes] = '\0';
        string message(buffer);

        if (!message.empty()) {
            auto& info = client_info[client_sock];
            string full_msg = "[" + to_string(info.second) + "] " + info.first + ": " + message;

            cout << "Message: " << full_msg << endl;
            broadcast_message(full_msg, client_sock);
        }
    }

    // Клиент отключился
    auto it = find(clients.begin(), clients.end(), client_sock);
    if (it != clients.end()) {
        clients.erase(it);
        client_count--;
    }

    if (client_info.find(client_sock) != client_info.end()) {
        string leave_msg = "[SERVER] " + client_info[client_sock].first + " left the chat";
        broadcast_message(leave_msg, INVALID_SOCKET);
        client_info.erase(client_sock);
    }

    socket_close(client_sock);
    cout << "Client disconnected. Total: " << client_count << endl;
}

int main() {
    cout << "=== Cross-platform Chat Server ===" << endl;

    if (!network_init()) {
        return 1;
    }

    int max_clients;
    cout << "Enter max clients: ";
    cin >> max_clients;
    cout << "Enter configuration password: ";
    cin >> Password;
    cin.ignore();

    // Создаем сокет
    socket_t server_sock = socket_create();
    if (server_sock == INVALID_SOCKET) {
        cerr << "Socket creation failed: " << socket_errno << endl;
        network_cleanup();
        return 1;
    }

    // Настройка адреса
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Разрешаем повторное использование порта
    int opt = 1;
    if (socket_setopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == SOCKET_ERROR) {
        cerr << "Set socket option failed: " << socket_errno << endl;
        socket_close(server_sock);
        network_cleanup();
        return 1;
    }

    // Привязываем сокет
    if (socket_bind(server_sock, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Bind failed: " << socket_errno << endl;
        cerr << "Make sure:" << endl;
        cerr << "1. Port " << PORT << " is available" << endl;
        cerr << "2. No other server is running" << endl;
        cerr << "3. You have permission to bind to this port" << endl;
        socket_close(server_sock);
        network_cleanup();
        return 1;
    }

    // Начинаем прослушивание
    if (socket_listen(server_sock, 10) == SOCKET_ERROR) {
        cerr << "Listen failed: " << socket_errno << endl;
        socket_close(server_sock);
        network_cleanup();
        return 1;
    }

    // Выводим информацию о сервере
    cout << "\n=== Server Information ===" << endl;
    cout << "Platform: ";
#ifdef _WIN32
    cout << "Windows" << endl;
#else
    cout << "Linux/Android" << endl;
#endif
    cout << "Port: " << PORT << endl;
    cout << "Max clients: " << max_clients << endl;
    cout << "Server IP: 0.0.0.0 (all interfaces)" << endl;
    cout << "Localhost: 127.0.0.1:" << PORT << endl;
    cout << "Waiting for connections..." << endl;
    cout << "Press Ctrl+C to stop server" << endl;
    cout << "==========================\n" << endl;

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        // Принимаем соединение
        socket_t client_sock = socket_accept(server_sock, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);

        if (client_sock == INVALID_SOCKET) {
            cerr << "Accept failed: " << socket_errno << endl;
            continue;
        }

        // Проверяем лимит клиентов
        if (client_count >= max_clients) {
            string error = "ERROR|Server is full. Max: " + to_string(max_clients);
            socket_send(client_sock, error.c_str(), error.length());
            socket_close(client_sock);
            cout << "Connection rejected: server full (" << max_clients << " clients)" << endl;
            continue;
        }

        // Добавляем клиента
        clients.push_back(client_sock);
        client_count++;

        // Выводим информацию о подключении
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        cout << "New connection from " << client_ip << ":" << ntohs(client_addr.sin_port) << endl;
        cout << "Total clients: " << client_count << "/" << max_clients << endl;

        // Запускаем обработчик в отдельном потоке
        thread client_thread(handle_client, client_sock);
        client_thread.detach();
    }

    // Закрываем серверный сокет (этот код никогда не выполнится в бесконечном цикле)
    socket_close(server_sock);
    network_cleanup();

    return 0;
}