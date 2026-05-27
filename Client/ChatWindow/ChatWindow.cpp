// ChatWindowClient.cpp - Œ“ƒ≈À‹Õ€… ‘¿…À ƒÀﬂ Œ Õ¿ ◊¿“¿
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

using namespace std;

// ------------------- ÷‚ÂÚÓ‚ÓÈ Ô‡ÒÂ -------------------
class ConsoleHelper {
public:
#ifdef _WIN32
    static void SetColor(int textColor, int bgColor = 0) {
        HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hStdOut, (WORD)((bgColor << 4) | textColor));
    }
    static void ResetColor() {
        SetColor(7);
    }
#endif
};

int hex_char_to_int(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

void print_colored_text(const string& text) {
    vector<int> text_stack = { 7 };
    vector<int> bg_stack = { 0 };
    size_t i = 0;
    size_t len = text.length();

    auto apply = [&]() {
        ConsoleHelper::SetColor(text_stack.back(), bg_stack.back());
        };

    while (i < len) {
        if (text[i] == '[' && i + 3 < len && text[i + 1] == 'c' && isxdigit(text[i + 2]) && text[i + 3] == ']') {
            int color = hex_char_to_int(text[i + 2]);
            if (color != -1) {
                text_stack.push_back(color);
                apply();
                i += 4;
                continue;
            }
        }
        else if (text[i] == '[' && i + 4 < len && text[i + 1] == '/' && text[i + 2] == 'c' && isxdigit(text[i + 3]) && text[i + 4] == ']') {
            if (text_stack.size() > 1) {
                text_stack.pop_back();
                apply();
            }
            i += 5;
            continue;
        }
        else if (text[i] == '[' && i + 4 < len && text[i + 1] == 'b' && text[i + 2] == 'g' && isxdigit(text[i + 3]) && text[i + 4] == ']') {
            int color = hex_char_to_int(text[i + 3]);
            if (color != -1) {
                bg_stack.push_back(color);
                apply();
                i += 5;
                continue;
            }
        }
        else if (text[i] == '[' && i + 5 < len && text[i + 1] == '/' && text[i + 2] == 'b' && text[i + 3] == 'g' && isxdigit(text[i + 4]) && text[i + 5] == ']') {
            if (bg_stack.size() > 1) {
                bg_stack.pop_back();
                apply();
            }
            i += 6;
            continue;
        }
        apply();
        cout << text[i];
        i++;
    }
    ConsoleHelper::ResetColor();
}
// ------------------------------------------------------

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

    send(sock, "CHAT_WINDOW", 11, 0);

    cout << "=== MCG Chat Window ===\n";
    cout << "Connected to MCG Client\n";
    cout << "All chat messages will appear here\n";
    cout << "Press Ctrl+C to exit\n";
    cout << "========================\n\n";

    char buffer[4096];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            print_colored_text(buffer);
        }
        else if (bytes == 0) {
            cout << "\n[INFO] Connection closed by server\n";
            break;
        }
        else {
            break;
        }
    }

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    cout << "\nChat window closed.\n";
    return 0;
}