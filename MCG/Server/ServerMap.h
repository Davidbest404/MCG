#pragma once
#include <string>
#include <winsock2.h>
#include "ServerShared.h"

// ----- Функции карты -----
int get_tile_id(int x, int y);
bool is_walkable(int x, int y);
void set_tile(int x, int y, int new_id);
void remove_tile(int x, int y);
void load_world_map(const std::string& filename);
void save_world_map(const std::string& filename);
void save_tiles(const std::string& filename);
void load_tiles(const std::string& filename);
char get_tile_char(int tile_id);
void call_tile_function(const std::string& func_name, int player_id, int x, int y, int tile_id);
void on_player_enter_tile(Player& player, int old_x, int old_y, int new_x, int new_y);
bool find_nearest_walkable(int tx, int ty, int& out_x, int& out_y);
bool correct_player_position(Player& player);
void correct_all_players_positions();