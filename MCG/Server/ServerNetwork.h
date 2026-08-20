#pragma once
#include <winsock2.h>
#include <string>

void broadcast_message(const std::string& message, SOCKET sender);
void handle_client(SOCKET client_sock);