#pragma once
#include <winsock2.h>
#include <string>
#include <variant>

void process_game_command(SOCKET client_sock, const std::string& command, int player_id, bool is_admin);

// Обработчики атрибутов
void set_attr_for_all(const std::string& attr_name, const std::variant<int, float, std::string, bool>& value);
std::variant<int, float, std::string, bool> parse_value(const std::string& value_str);
void handle_set_attr_all(SOCKET client_sock, const std::string& command);
void handle_set_attr(SOCKET client_sock, const std::string& command, int caller_id, bool is_admin);
void handle_get_attr(SOCKET client_sock, const std::string& command);
void handle_has_attr(SOCKET client_sock, const std::string& command);
void handle_remove_attr(SOCKET client_sock, const std::string& command, int caller_id, bool is_admin);
void handle_remove_attr_all(SOCKET client_sock, const std::string& command);
void handle_default_attr_add(SOCKET client_sock, const std::string& command);
void handle_default_attr_remove(SOCKET client_sock, const std::string& command);
void handle_default_attr_list(SOCKET client_sock);
void handle_sync_default_attrs(SOCKET client_sock);
void handle_list_attrs(SOCKET client_sock, const std::string& command, int caller_id);
void handle_edit_description(SOCKET client_sock, const std::string& command);
void handle_edit_rules(SOCKET client_sock, const std::string& command);
void handle_description(SOCKET client_sock);
void handle_rules(SOCKET client_sock);
bool save_lua_preset(const std::string& preset_name, std::string& error_msg);
bool load_lua_preset(const std::string& preset_name, std::string& error_msg);