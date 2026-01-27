#include <iostream>
#include <cstring>
#include <thread>
#include <string>
#include <atomic>

// Платформо-зависимые заголовки
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")
#define SOCKET_TYPE SOCKET
#define INVALID_SOCKET_VAL INVALID_SOCKET
#define CLOSE_SOCKET closesocket
#define GET_LAST_ERROR WSAGetLastError()
#define SLEEP(ms) Sleep(ms)
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#define SOCKET_TYPE int
#define INVALID_SOCKET_VAL -1
#define CLOSE_SOCKET close
#define GET_LAST_ERROR errno
#define SLEEP(ms) usleep(ms * 1000)
#endif

using namespace std;

const int PORT = 8080;
atomic<bool> running(true);

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

// Функция приёма сообщений
void receive_messages(SOCKET_TYPE client_socket) {
    char buffer[1024];

    while (running) {
        memset(buffer, 0, sizeof(buffer));

#ifdef _WIN32
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
#else
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
#endif

        if (bytes_received <= 0) {
            if (running) {
                cout << "\n[INFO] Disconnected from server." << endl;
            }
            break;
        }

        buffer[bytes_received] = '\0';
        cout << buffer << endl;
        cout << "> " << flush;
    }
}

// Проверка подключения
bool is_connected(SOCKET_TYPE sock) {
    if (sock == INVALID_SOCKET_VAL) return false;

#ifdef _WIN32
    fd_set readfds;
    timeval timeout = { 0, 1000 };
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    int result = select(0, &readfds, nullptr, nullptr, &timeout);
    return result != SOCKET_ERROR;
#else
    fd_set readfds;
    timeval timeout = { 0, 1000 };
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    int result = select(sock + 1, &readfds, nullptr, nullptr, &timeout);
    return result >= 0;
#endif
}

int main() {
    if (!network_init()) {
        cerr << "Network initialization failed!" << endl;
        return 1;
    }

    cout << "=== Cross-platform Chat Client ===" << endl;
#ifdef _WIN32
    cout << "Platform: Windows" << endl;
#else
    cout << "Platform: Linux/Android" << endl;
#endif
    cout << "Server port: " << PORT << endl;
    cout << "Commands: disconnect, exit, /help, /list" << endl;
    cout << "Game commands: /move, /attack, /defend, /skip, /status, /map, /ready, /unready" << endl;
    cout << "==================================" << endl;

    while (running) {
        string ip_address;
        cout << "\nEnter server IP (127.0.0.1 for localhost): ";
        getline(cin, ip_address);

        if (ip_address == "exit") {
            running = false;
            break;
        }

        if (ip_address.empty()) {
            ip_address = "127.0.0.1";
        }

        SOCKET_TYPE client_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (client_socket == INVALID_SOCKET_VAL) {
            cerr << "Socket creation failed: " << GET_LAST_ERROR << endl;
            continue;
        }

        sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(PORT);

        if (inet_pton(AF_INET, ip_address.c_str(), &server_addr.sin_addr) <= 0) {
            cerr << "Invalid IP address." << endl;
            CLOSE_SOCKET(client_socket);
            continue;
        }

        cout << "Connecting to " << ip_address << "..." << endl;

        if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            cerr << "Connection failed: " << GET_LAST_ERROR << endl;
            CLOSE_SOCKET(client_socket);
            continue;
        }

        cout << "Connected! Waiting for server welcome..." << endl << endl;

        // Ждем приветственное сообщение от сервера
        char welcome_buffer[512];
        memset(welcome_buffer, 0, sizeof(welcome_buffer));

#ifdef _WIN32
        int welcome_bytes = recv(client_socket, welcome_buffer, sizeof(welcome_buffer) - 1, 0);
#else
        ssize_t welcome_bytes = recv(client_socket, welcome_buffer, sizeof(welcome_buffer) - 1, 0);
#endif

        if (welcome_bytes > 0) {
            welcome_buffer[welcome_bytes] = '\0';
            cout << welcome_buffer << endl;  // Выводим приветствие от сервера
        }

        // Аутентификация
        bool authenticated = false;
        while (!authenticated && running) {
            string auth_msg;
            cout << "> ";
            getline(cin, auth_msg);

            if (auth_msg == "disconnect" || auth_msg == "exit") {
                if (auth_msg == "exit") running = false;
                break;
            }

            if (send(client_socket, auth_msg.c_str(), auth_msg.size(), 0) < 0) {
                cerr << "Send failed." << endl;
                break;
            }

            char response[256];
            memset(response, 0, sizeof(response));

#ifdef _WIN32
            int bytes = recv(client_socket, response, sizeof(response) - 1, 0);
#else
            ssize_t bytes = recv(client_socket, response, sizeof(response) - 1, 0);
#endif

            if (bytes > 0) {
                response[bytes] = '\0';
                string resp_str(response);
                if (resp_str.find("OK") == 0) {
                    cout << "Authentication successful!" << endl;
                    authenticated = true;
                }
                else {
                    cout << "Error: " << resp_str << endl;
                }
            }
            else {
                cerr << "Server disconnected." << endl;
                break;
            }
        }

        if (!authenticated) {
            CLOSE_SOCKET(client_socket);
            continue;
        }

        cout << "\n=== Chat Started ===" << endl;
        cout << "Type your messages:" << endl;

        thread receive_thread(receive_messages, client_socket);

        string message;
        while (running && is_connected(client_socket)) {
            cout << "> ";
            getline(cin, message);

            if (message == "disconnect") {
                cout << "Disconnecting..." << endl;
                break;
            }

            if (message == "exit") {
                running = false;
                break;
            }


            // Локальные команды клиента
            if (message == "/help") {
                cout << "\n=== Client Commands ===\n";
                cout << "/help - Show this message\n";
                cout << "/list - List all connected clients\n";
                cout << "\n=== Game Commands ===\n";
                cout << "/move [direction] - Move (north, south, east, west)\n";
                cout << "/attack [target_id] - Attack another player\n";
                cout << "/defend - Defend yourself\n";
                cout << "/skip - Skip your turn\n";
                cout << "/status - Check your status\n";
                cout << "/map - Show game map\n";
                cout << "/ready - Mark yourself as ready\n";
                cout << "/unready - Mark yourself as not ready\n";
                cout << "=======================\n";
                continue;
            }

            if (send(client_socket, message.c_str(), message.size(), 0) < 0) {
                cerr << "Send failed." << endl;
                break;
            }
        }

        running = false;
        if (receive_thread.joinable()) {
            receive_thread.join();
        }
        running = true;

        CLOSE_SOCKET(client_socket);

        if (message != "exit") {
            cout << "Disconnected. Connect to another server?" << endl;
        }
    }

    network_cleanup();
    cout << "Client terminated." << endl;
    return 0;
}