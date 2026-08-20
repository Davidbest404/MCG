#include "Utils.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>

bool is_valid_port(const std::string& port_str, int& port_out) {
    if (port_str.empty()) return false;
    try {
        size_t pos;
        int port = std::stoi(port_str, &pos);
        if (pos != port_str.length()) return false;
        if (port < 1 || port > 65535) return false;
        port_out = port;
        return true;
    }
    catch (...) {
        return false;
    }
}

bool is_valid_ip(const std::string& ip) {
    if (ip == "localhost" || ip == "127.0.0.1") return true;
    struct in_addr addr;
    int result = inet_pton(AF_INET, ip.c_str(), &addr);
    return result == 1;
}

bool network_init() {
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

void network_cleanup() {
    WSACleanup();
}