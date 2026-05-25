#include <iostream>
#include <thread>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

using namespace std;

const char* LOCAL_IP = "127.0.0.1"; // IP
char* SERVER_IP; // IP сервера
int PORT = 8080;

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

void receive_messages(int sock) {
    char buffer[1024];
    while (true) {
        int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) break;
        buffer[bytes_received] = '\0';
        cout << "Received: " << buffer << endl;
    }
}

int main() {
    string Server_IP;
    cin >> Server_IP;
    if (Server_IP.empty())
    {
        Server_IP = LOCAL_IP;
    }
    int Port;
    cin >> Port;
    if (Port == NULL)
    {
        Port = PORT;
    }
    init_winsock();

    int sockfd;
#ifdef _WIN32
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
#endif

    if (sockfd < 0) {
        cerr << "Failed to create socket" << endl;
        cleanup_winsock();
        return 1;
    }

    sockaddr_in server_addr;
    // Инициализация адреса сервера
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

#ifdef _WIN32
#if defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0600)
    inet_pton(AF_INET, Server_IP.c_str(), &server_addr.sin_addr);
#else
    server_addr.sin_addr.s_addr = inet_addr(Server_IP.c_str());
#endif
#else
    inet_pton(AF_INET, Server_IP.c_str(), &server_addr.sin_addr);
#endif

    if (connect(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
        cerr << "Failed to connect to server" << endl;
#ifdef _WIN32
        closesocket(sockfd);
        cleanup_winsock();
#else
        close(sockfd);
#endif
        return 1;
    }

    // Запуск потока для получения сообщений
    thread receiver(receive_messages, sockfd);
    receiver.detach();

    // Основной цикл для отправки сообщений
    string msg;
    while (true) {
        getline(cin, msg);
        if (msg == "exit") break; // чтобы выйти из клиента
        send(sockfd, msg.c_str(), msg.size(), 0);
    }

    // Закрытие сокета
#ifdef _WIN32
    closesocket(sockfd);
    cleanup_winsock();
#else
    close(sockfd);
#endif

    return 0;
}