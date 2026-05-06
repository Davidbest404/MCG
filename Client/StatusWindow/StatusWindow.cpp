// StatusWindowClient.cpp - Œ“ƒ≈À‹Õ€… ‘¿…À ƒÀﬂ Œ Õ¿ —“¿“”—¿
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#include <windows.h>
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

using namespace std;

void clear_screen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        cerr << "Socket creation failed\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9090);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "Connection failed. Make sure MCG_Client is running.\n";
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return 1;
    }

    // ŒÚÔ‡‚ÎˇÂÏ ÚËÔ ÓÍÌ‡
    send(sock, "STATUS_WINDOW", 13, 0);

    cout << "=== MCG Status Window ===\n";
    cout << "Connected to MCG Client\n";
    cout << "Player status will appear here\n";
    cout << "Press Ctrl+C to exit\n";
    cout << "=========================\n\n";

    char buffer[4096];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            clear_screen();
            cout << "=== MCG Status Window ===\n\n";
            cout << buffer;
        }
        else if (bytes == 0) {
            cout << "\n[INFO] Connection closed by server\n";
            break;
        }
        else {
            // Œ¯Ë·Í‡ ÔËÂÏ‡
            break;
        }
    }

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    cout << "\nStatus window closed.\n";
    return 0;
}