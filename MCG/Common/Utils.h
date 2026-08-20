#pragma once
#include <string>

bool is_valid_port(const std::string& port_str, int& port_out);
bool is_valid_ip(const std::string& ip);
bool network_init();
void network_cleanup();