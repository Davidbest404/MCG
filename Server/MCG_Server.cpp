// CrossPlatformServer.cpp
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

const int PORT = 8080;
std::vector<int> clients;
std::mutex clients_mutex;

#ifdef _WIN32
void init_winsock() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}
void cleanup_winsock() {
    WSACleanup();
}
#else
void init_winsock() {}
void cleanup_winsock() {}
#endif

void broadcast_message(const std::string& message, int sender_fd) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (int client_fd : clients) {
        if (client_fd != sender_fd) {
            send(client_fd, message.c_str(), message.size(), 0);
        }
    }
}

void handle_client(int client_fd) {
    char buffer[1024];
    while (true) {
        int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) break;
        buffer[bytes_received] = '\0';
        std::string msg = "Client: " + std::string(buffer);
        std::cout << msg << std::endl;
        broadcast_message(msg, client_fd);
    }
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.erase(std::remove(clients.begin(), clients.end(), client_fd), clients.end());
    }
#ifdef _WIN32
    closesocket(client_fd);
#else
    close(client_fd);
#endif
}

int main() {
    int Port;
    std::cin >> Port;
    if (Port == NULL)
    {
        Port = PORT;
    }
    init_winsock();

#ifdef _WIN32
    int server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(Port);
    bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_fd, 10);
#else
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(Port);
    bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_fd, 10);
#endif

    std::cout << "Server started on port " << Port << std::endl;

    while (true) {
#ifdef _WIN32
        int addrlen = sizeof(sockaddr_in);
        sockaddr_in client_addr;
        int client_fd = accept(server_fd, (sockaddr*)&client_addr, &addrlen);
#else
        socklen_t addrlen = sizeof(sockaddr_in);
        sockaddr_in client_addr;
        int client_fd = accept(server_fd, (sockaddr*)&client_addr, &addrlen);
#endif
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients.push_back(client_fd);
        }
        std::thread(handle_client, client_fd).detach();
    }

#ifdef _WIN32
    closesocket(server_fd);
    cleanup_winsock();
#else
    close(server_fd);
#endif
    return 0;
}