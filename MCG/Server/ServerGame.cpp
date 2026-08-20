#include "ServerGame.h"
#include "ServerShared.h"
#include "ServerMap.h"
#include "../Common/ConsoleHelper.h"
#include <thread>
#include <chrono>
#include <fstream>
#include <iostream>

using namespace std;

// Внешние зависимости
extern GameState game_state;
extern mutex game_mutex;
extern mutex default_attrs_mutex;
extern unordered_map<string, variant<int, float, string, bool>> default_attrs;
extern atomic<int> next_client_id;
extern map<int, Tile> tile_types;

// ----- Реализации -----

void apply_default_attrs(Player& player) {
    lock_guard<mutex> lock(default_attrs_mutex);
    for (const auto& [key, val] : default_attrs) {
        player.setAttr(key, val);
    }
}

void remove_attr_from_all(const string& attr_name) {
    lock_guard<mutex> lock(game_mutex);
    for (auto& [id, player] : game_state.players) {
        player.removeAttr(attr_name);
    }
}

int Random(int max, int min) {
    return rand() % (max - min + 1) + min;
}

void game_timer_thread() {
    while (true) {
        this_thread::sleep_for(chrono::seconds(1));
        bool should_process_turn = false;
        {
            lock_guard<mutex> lock(game_mutex);
            if (game_state.is_active) {
                time_t current_time = time(nullptr);
                time_t elapsed = current_time - game_state.turn_start_time;
                if (elapsed >= game_state.turn_duration_seconds) {
                    should_process_turn = true;
                }
            }
        }
        if (should_process_turn) {
            process_turn_end();
            int minutes = static_cast<int>(game_state.turn_duration_seconds / 60);
            int seconds = static_cast<int>(game_state.turn_duration_seconds - (minutes * 60));
            if (seconds >= 10) {
                broadcast_message("\n=== Turn automatically ended by timer ===\n", INVALID_SOCKET);
            }
        }
    }
}

void process_turn_end() {
    lock_guard<mutex> lock(game_mutex);
    bool all_ready = true;
    for (auto& pair : game_state.players) {
        if (!pair.second.is_ready) {
            all_ready = false;
            break;
        }
    }

    string turn_summary = "\n=== Turn " + to_string(game_state.current_turn) + " Summary ===\n";
    for (auto& pair : game_state.players) {
        auto& p = pair.second;
        switch (p.last_action) {
        case ActionType::MOVE:
            turn_summary += p.name + " moved to position (" + to_string(p.x) + "," + to_string(p.y) + ")\n";
            break;
        case ActionType::LUA:
            turn_summary += p.name + " used dynamic(Lua) command\n";
            break;
        default:
            turn_summary += p.name + " waited.\n";
        }
        if (p.last_action != ActionType::MOVE) {
            int tid = get_tile_id(p.x, p.y);
            if (tid != 0) {
                auto it = tile_types.find(tid);
                if (it != tile_types.end() && !it->second.on_step.empty()) {
                    call_tile_function(it->second.on_step, p.id, p.x, p.y, tid);
                }
            }
        }
        p.is_ready = false;
    }
    turn_summary += "=============================\n";
    game_state.turn_log.push_back(turn_summary);
    game_state.current_turn++;
    game_state.turn_start_time = time(nullptr);
}

void send_time_remaining(SOCKET client_sock) {
    if (!game_state.is_active) return;
    time_t current_time = time(nullptr);
    time_t elapsed = current_time - game_state.turn_start_time;
    time_t remaining = game_state.turn_duration_seconds - elapsed;
    if (remaining > 0) {
        int minutes = static_cast<int>(remaining / 60);
        int seconds = static_cast<int>(remaining % 60);
        string time_msg = "Time remaining: " + to_string(minutes) + "m " + to_string(seconds) + "s\n";
        send(client_sock, time_msg.c_str(), static_cast<int>(time_msg.length()), 0);
    }
}

void save_game_state(const string& filename) {
    ofstream file(filename);
    if (!file.is_open()) {
        cerr << "Failed to save game state to " << filename << endl;
        return;
    }
    lock_guard<mutex> lock(game_mutex);

    file << 2 << endl;
    file << game_state.current_turn << endl;
    file << game_state.turn_duration_seconds << endl;
    file << game_state.is_active << endl;
    file << game_state.players.size() << endl;

    for (auto& pair : game_state.players) {
        auto& player = pair.second;
        file << player.name << " "
            << player.hp << " "
            << player.max_hp << " "
            << player.x << " "
            << player.y << " "
            << player.is_admin << " "
            << player.can_move;

        file << " " << player.attrs.size();
        for (const auto& attr : player.attrs) {
            file << " " << attr.first;
            if (holds_alternative<int>(attr.second)) {
                file << " int " << get<int>(attr.second);
            }
            else if (holds_alternative<float>(attr.second)) {
                file << " float " << get<float>(attr.second);
            }
            else if (holds_alternative<string>(attr.second)) {
                file << " string " << get<string>(attr.second);
            }
            else if (holds_alternative<bool>(attr.second)) {
                file << " bool " << (get<bool>(attr.second) ? 1 : 0);
            }
        }
        file << endl;
    }

    int log_size = min(10, (int)game_state.turn_log.size());
    file << log_size << endl;
    for (int i = 0; i < log_size; i++) {
        file << game_state.turn_log[game_state.turn_log.size() - log_size + i] << "|||";
    }
    file.close();
    ConsoleHelper::SetColor(6);
    cout << "Game state saved to " << filename << endl;
    ConsoleHelper::SetColor(8);
}

void load_game_state(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Failed to load game state from " << filename << endl;
        return;
    }
    lock_guard<mutex> lock(game_mutex);
    game_state.players.clear();
    game_state.turn_log.clear();
    next_client_id = 1;

    int version;
    file >> version;
    if (version != 2) {
        cerr << "Unsupported save file version (expected 2, got " << version << "). Aborting load." << endl;
        return;
    }

    file >> game_state.current_turn;
    file >> game_state.turn_duration_seconds;
    file >> game_state.is_active;
    int player_count;
    file >> player_count;

    for (int i = 0; i < player_count; i++) {
        Player player;
        file >> player.name
            >> player.hp
            >> player.max_hp
            >> player.x
            >> player.y
            >> player.is_admin
            >> player.can_move;

        size_t attr_count;
        file >> attr_count;
        for (size_t j = 0; j < attr_count; ++j) {
            string key, typeStr;
            file >> key >> typeStr;
            if (typeStr == "int") {
                int val; file >> val;
                player.setAttr(key, val);
            }
            else if (typeStr == "float") {
                float val; file >> val;
                player.setAttr(key, val);
            }
            else if (typeStr == "string") {
                string val; file >> val;
                player.setAttr(key, val);
            }
            else if (typeStr == "bool") {
                int val; file >> val;
                player.setAttr(key, (bool)val);
            }
            else {
                cerr << "Unknown attribute type: " << typeStr << ", skipping key " << key << endl;
            }
        }

        int new_id = next_client_id++;
        player.id = new_id;
        game_state.players[new_id] = player;

        correct_all_players_positions();

        ConsoleHelper::SetColor(4);
        cout << "Loaded player: " << player.name
            << " (HP: " << player.hp << "/" << player.max_hp
            << ")" << endl;
        ConsoleHelper::SetColor(8);
    }

    int log_size;
    file >> log_size;
    file.ignore();
    for (int i = 0; i < log_size; i++) {
        string log_entry;
        getline(file, log_entry, '|');
        if (!log_entry.empty()) {
            game_state.turn_log.push_back(log_entry);
        }
        file.ignore(2);
    }
    file.close();
    ConsoleHelper::SetColor(6);
    cout << "Game state loaded from " << filename << ". " << player_count << " players restored." << endl;
    ConsoleHelper::SetColor(8);
}

void auto_save_thread() {
    while (true) {
        this_thread::sleep_for(chrono::minutes(15));
        if (game_state.is_active) {
            save_game_state("autosave.mcgsave");
            save_tiles("autosave_tiles.mcgtile");
            ConsoleHelper::SetColor(10);
            cout << "Auto-save completed (game + tiles)" << endl;
            ConsoleHelper::SetColor(8);
        }
    }
}