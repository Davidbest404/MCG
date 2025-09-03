#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <vector>
#include <string>

#pragma comment(lib, "Ws2_32.lib")

const int PORT = 8080;

std::vector<SOCKET> clients;
int num_clients = 0; // Переменная для хранения количества подключенных клиентов

void broadcast_message(std::string message, SOCKET sender)
{
    for (auto client : clients)
    {
        if (client != sender)
        {
            send(client, message.c_str(), message.size() + 1, 0);
        }
    }
}

void handle_client(SOCKET client_socket)
{
    char buffer[1024];
    while (true)
    {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0)
        {
            std::cout << "Client disconnected." << std::endl;
            closesocket(client_socket);
            clients.erase(std::remove(clients.begin(), clients.end(), client_socket), clients.end());
            num_clients--;
            std::cout << "Current number of clients: " << num_clients << std::endl;
            break;
        }

        std::string message(buffer, bytes_received);
        std::cout << "Received from client: " << message << std::endl;
        broadcast_message(message, client_socket);
    }
}

int main()
{
    int MAX_CLIENTS;
    std::cout << "Write max clients on server: ";
    std::cin >> MAX_CLIENTS;
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET)
    {
        std::cerr << "Failed to create socket." << std::endl;
        return 1;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
    {
        std::cerr << "Failed to bind socket." << std::endl;
        closesocket(server_socket);
        return 1;
    }

    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "Failed to listen on socket." << std::endl;
        closesocket(server_socket);
        return 1;
    }

    std::cout << "Server is listening on port " << PORT << "." << std::endl;

    while (true)
    {
        SOCKET client_socket = accept(server_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET)
        {
            std::cerr << "Failed to accept connection." << std::endl;
            continue;
        }

        // Проверка на максимальное количество клиентов
        if (num_clients >= MAX_CLIENTS)
        {
            std::cerr << "Maximum number of clients reached. Connection rejected." << std::endl;

            // Отправляем сообщение клиенту о превышении лимита
            const char* error_message = "Connection refused: maximum number of clients reached.\n";
            send(client_socket, error_message, strlen(error_message), 0);

            closesocket(client_socket); // Закрываем соединение, если лимит достигнут
            continue;
        }

        clients.push_back(client_socket);
        num_clients++;
        std::cout << "New client connected. Current number of clients: " << num_clients << std::endl;

        std::thread client_thread(handle_client, client_socket);
        client_thread.detach();
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}