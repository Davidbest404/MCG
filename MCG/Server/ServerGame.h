#pragma once
#include <winsock2.h>
#include <string>
#include "ServerShared.h"   // <-- днаюбкемн

void game_timer_thread();
void process_turn_end();
void send_time_remaining(SOCKET client_sock);
void apply_default_attrs(Player& player);
void remove_attr_from_all(const std::string& attr_name);
int Random(int max, int min);
void save_game_state(const std::string& filename);
void load_game_state(const std::string& filename);
void auto_save_thread();