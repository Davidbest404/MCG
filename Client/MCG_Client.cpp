#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <string>

#pragma comment(lib, "Ws2_32.lib")

const int PORT = 8080;

void receive_messages(SOCKET client_socket)
{
    char buffer[1024];
    while (true)
    {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received > 0)
        {
            std::string message(buffer, bytes_received);
            std::cout << message << std::endl;
        }
    }
}

int main()
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    std::string ip_address;
    std::cout << "Enter server IP address: ";
    std::getline(std::cin, ip_address);

    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket == INVALID_SOCKET)
    {
        std::cerr << "Failed to create socket." << std::endl;
        return 1;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, ip_address.c_str(), &server_addr.sin_addr);

    if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
    {
        std::cerr << "Failed to connect to server." << std::endl;
        closesocket(client_socket);
        return 1;
    }

    std::thread receive_thread(receive_messages, client_socket);

    std::string message;
    while (true)
    {
        std::getline(std::cin, message);
        send(client_socket, message.c_str(), message.size() + 1, 0);
    }

    closesocket(client_socket);
    WSACleanup();
    return 0;
}