#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <random>

// Кроссплатформенные определения socket_t
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
typedef SOCKET socket_t;
#define socket_close closesocket
#define socket_errno WSAGetLastError()
typedef long ssize_t;
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
typedef int socket_t;
#define socket_close close
#define socket_errno errno
#endif

using namespace std;

// Константы портов
const int PORT = 8080;

// Глобальные переменные
atomic<bool> running(true);
vector<socket_t> clients;
mutex clients_mutex;

// Функция для отправки данных
bool socket_send(socket_t sock, const string& data) {
    ssize_t result = 0;
#ifdef _WIN32
    result = send(sock, data.c_str(), static_cast<int>(data.size()), 0);
#else
    result = send(sock, data.c_str(), data.size(), 0);
#endif
    return result >= 0;
}

// Получение данных
string socket_receive(socket_t sock, int timeout_ms = 1000) {
    // Установка таймаута
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
    ssize_t received = 0;
#ifdef _WIN32
    received = recv(sock, buffer, sizeof(buffer) - 1, 0);
#else
    received = recv(sock, buffer, sizeof(buffer) - 1, 0);
#endif
    if (received > 0) {
        buffer[received] = '\0';
        return string(buffer);
    }
    return "";
}

// Создание accept-соединения
socket_t accept_connection(socket_t server_socket) {
#ifdef _WIN32
    int addr_len = sizeof(sockaddr_in);
    return accept(server_socket, nullptr, &addr_len);
#else
    socklen_t addr_len = sizeof(sockaddr_in);
    return accept(server_socket, nullptr, &addr_len);
#endif
}

// Обработка клиента
void handle_client(socket_t client_socket)
{
    char buffer[1024];

    while (true)
    {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);

        if (bytes_received <= 0)
        {
            // Клиент отключился или произошла ошибка
            break;
        }

        std::string message(buffer, bytes_received);

        // Обработка команды /disconnect
        if (message == "/disconnect")
        {
            // Сообщение клиенту (опционально)
            std::string disconnect_msg = "Disconnecting...\n";
            send(client_socket, disconnect_msg.c_str(), disconnect_msg.size() + 1, 0);

            // Выходим из цикла, чтобы закрыть сокет
            break;
        }

        // Рассылка сообщения другим клиентам
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto& client : clients)
        {
            if (client != client_socket)
            {
                send(client, message.c_str(), message.size() + 1, 0);
            }
        }
    }

    // После выхода из цикла закрываем сокет
    socket_close(client_socket);
    // Удаляем клиента из списка
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = std::find(clients.begin(), clients.end(), client_socket);
        if (it != clients.end())
            clients.erase(it);
    }
    std::cout << "Client disconnected." << std::endl;
}

int main() {
    // Инициализация сети
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    // Создаем сокет
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "[ERROR] Socket creation failed." << std::endl;
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    // Подготовка адреса
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    // Связываем
    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[ERROR] Bind failed." << std::endl;
        socket_close(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    // Слушаем
    if (listen(sock, 5) < 0) {
        std::cerr << "[ERROR] Listen failed." << std::endl;
        socket_close(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "Listening on port " << PORT << "...\n";

    // Основной цикл
    while (running)
    {
        socket_t client_sock = accept_connection(sock);
        if (client_sock < 0)
        {
            std::cerr << "[ERROR] Accept failed." << std::endl;
            continue;
        }
        std::cout << "Client connected.\n";

        // Добавляем клиента
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients.push_back(client_sock);
        }

        // Обработка клиента в отдельном потоке
        std::thread(handle_client, client_sock).detach();
    }

    // Очистка
    socket_close(sock);
#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}