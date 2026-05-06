// MCG_Server.cpp - with IPv6 support
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <thread>
#include <mutex>

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

vector<socket_t> clients;
int num_clients = 0;
map<socket_t, pair<string, int>> users_and_ids;
int next_id = 1;

void add_user(socket_t client_socket, const std::string& username)
{
    users_and_ids[client_socket] = make_pair(username, next_id++);
}

void broadcast_message(const std::string& message, socket_t sender)
{
    for (auto client : clients)
    {
        if (client != sender)
        {
            send(client, message.c_str(), message.size() + 1, 0);
        }
    }
}

bool check_credentials(const std::string& username, const std::string& password)
{
    return true;
}

void handle_client(socket_t client_socket)
{
    char buffer[1024];
    bool authenticated = false;

    while (!authenticated)
    {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0)
        {
            cout << "Client disconnected during authentication." << endl;
            closesocket(client_socket);
            return;
        }

        string packet(buffer, bytes_received);
        size_t pos = packet.find('|');
        if (packet.substr(0, pos) == "AUTH")
        {
            string credentials = packet.substr(pos + 1);
            size_t next_pos = credentials.find('|');
            string username = credentials.substr(0, next_pos);
            string password = credentials.substr(next_pos + 1);

            if (check_credentials(username, password))
            {
                authenticated = true;
                add_user(client_socket, username);
                send(client_socket, "OK", 2, 0);
            }
            else
            {
                string error_message = "Invalid format. Use AUTH|username|password\n";
                send(client_socket, error_message.c_str(), error_message.size() + 1, 0);
            }
        }
        else
        {
            string error_message = "Incorrect command format. Please use AUTH|username|password\n";
            send(client_socket, error_message.c_str(), error_message.size() + 1, 0);
        }
    }

    while (true)
    {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0)
        {
            cout << "Client disconnected." << endl;
            closesocket(client_socket);
            clients.erase(remove(clients.begin(), clients.end(), client_socket), clients.end());
            num_clients--;
            cout << "Current number of clients: " << num_clients << endl;
            break;
        }

        string message(buffer, bytes_received);
        int user_id = users_and_ids[client_socket].second;
        string username = users_and_ids[client_socket].first;

        string signed_message = "[" + to_string(user_id) + "] " + username + ": " + message;
        broadcast_message(signed_message, client_socket);
    }
}

int main()
{
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    int MAX_CLIENTS;
    cout << "Write max clients on server: ";
    cin >> MAX_CLIENTS;
    cin.ignore();

    // Create IPv6 socket
    socket_t server_socket = socket(AF_INET6, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET)
    {
        cerr << "Failed to create socket." << endl;
        return 1;
    }

    // Enable dual-stack (IPv4 & IPv6) if needed
    int dual_stack = 0; // 0 disables dual-stack, 1 enables
#ifdef _WIN32
    setsockopt(server_socket, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&dual_stack, sizeof(dual_stack));
#else
    setsockopt(server_socket, IPPROTO_IPV6, IPV6_V6ONLY, &dual_stack, sizeof(dual_stack));
#endif

    sockaddr_in6 server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_port = htons(PORT);
    server_addr.sin6_addr = in6addr_any; // bind to all interfaces (IPv6 and IPv4 if dual-stack enabled)

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
    {
        cerr << "Failed to bind socket." << endl;
        closesocket(server_socket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR)
    {
        cerr << "Failed to listen on socket." << endl;
        closesocket(server_socket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    cout << "Server is listening on port " << PORT << " with IPv6 support." << endl;

    while (true)
    {
        sockaddr_in6 client_addr;
        socklen_t addr_len = sizeof(client_addr);
        socket_t client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        if (client_socket == INVALID_SOCKET)
        {
            cerr << "Failed to accept connection." << endl;
            continue;
        }

        if (num_clients >= MAX_CLIENTS)
        {
            const char* error_message = "Connection refused: maximum number of clients reached.\n";
            send(client_socket, error_message, strlen(error_message), 0);
            closesocket(client_socket);
            continue;
        }

        clients.push_back(client_socket);
        num_clients++;
        cout << "New client connected. Current number of clients: " << num_clients << endl;

        thread(handle_client, client_socket).detach();
    }

    // Cleanup
#ifdef _WIN32
    closesocket(server_socket);
    WSACleanup();
#else
    close(server_socket);
#endif

    return 0;
}