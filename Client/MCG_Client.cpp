// MCG_Client.cpp
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
typedef SOCKET socket_t;
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
typedef int socket_t;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

using namespace std;

const int PORT = 8080;

void receive_messages(socket_t sock)
{
    char buffer[1024];
    while (true)
    {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(sock, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0)
        {
            break;
        }
        else
        {
            cout << string(buffer, bytes_received) << endl;
        }
    }
}

int main()
{
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    std::string ip_address;
    socket_t sock = INVALID_SOCKET;

    while (true)
    {
        std::cout << "Enter server IP address (or type 'exit' to quit): ";
        getline(cin, ip_address);
        if (ip_address == "exit" || ip_address == "Exit")
        {
            break; // Выход из программы
        }

        // Создаем сокет и подключаемся
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET)
        {
            std::cerr << "Failed to create socket." << std::endl;
            continue;
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(PORT);
        inet_pton(AF_INET, ip_address.c_str(), &server_addr.sin_addr);

        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
        {
            std::cerr << "Failed to connect to server." << std::endl;
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
            continue; // Попытка снова подключиться
        }
        bool connected = true;
        std::cout << "Connected to the server." << std::endl;

        // Запускаем поток для получения сообщений
        std::thread receiver(receive_messages, sock);

        // Ввод сообщений
        while (connected)
        {
            std::string message;
            getline(cin, message);
            if (message == "disconnect")
            {
                // Не завершаем программу, а закрываем соединение и возвращаемся к вводу IP
                std::cout << "Disconnecting from server...\n";
                std::cout << "It may crush programm...\n";
                connected = false;
            }
            else if (message == "Exit" || message == "exit")
            {
                // Завершение работы программы
                std::cout << "Exiting program.\n";
                break;
            }
            else
            {
                // Отправляем сообщение
                send(sock, message.c_str(), message.size() + 1, 0);
            }
        }

        // Остановка получения сообщений
        // Здесь можно завершить поток или просто оставить его завершиться при разрыве соединения
        // Для простоты, пусть поток завершится при закрытии сокета

        // Закрываем сокет перед следующей попыткой подключения
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        sock = INVALID_SOCKET;
    }

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}