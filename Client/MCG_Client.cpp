#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <string>

#pragma comment(lib, "Ws2_32.lib")

const int PORT = 8080;

using namespace std;

// Функция приёма сообщений от сервера
void receive_messages(SOCKET client_socket)
{
    char buffer[1024];
    while (true)
    {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0)
        {
            break; // Сервер завершил работу или произошел разрыв
        }
        else
        {
            string message(buffer, bytes_received);
            cout << message << endl;
        }
    }
}

bool is_connected(SOCKET sock)
{
    fd_set readfds;
    timeval timeout = { 0 };
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    select((int)(sock + 1), &readfds, nullptr, nullptr, &timeout);
    return !FD_ISSET(sock, &readfds);
}

int main()
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET client_socket = INVALID_SOCKET;
    bool running = true;

    while (running)
    {
        string ip_address;
        cout << "Enter server IP address: ";
        getline(cin, ip_address);

        // Создание сокета
        client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (client_socket == INVALID_SOCKET)
        {
            cerr << "Failed to create socket." << endl;
            continue;
        }

        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(PORT);
        inet_pton(AF_INET, ip_address.c_str(), &server_addr.sin_addr);

        // Подключение к серверу
        if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
        {
            cerr << "Failed to connect to server." << endl;
            closesocket(client_socket);
            continue;
        }

        cout << "Connected to the server." << endl;

        // Запуск потока для приёма сообщений
        thread receive_thread(receive_messages, client_socket);

        string message;
        while (is_connected(client_socket))
        {
            getline(cin, message);
            if (message == "disconnect")
            {
                cout << "Disconnecting from server." << endl;
                break;
            }
            send(client_socket, message.c_str(), message.size() + 1, 0);
        }

        // Прерывание приема сообщений
        receive_thread.detach(); // Отделяем поток, иначе join может заблокироваться

        closesocket(client_socket);
        client_socket = INVALID_SOCKET;

        cout << "Disconnected from server. Enter new IP address or exit.\n";
    }

    WSACleanup();
    return 0;
}