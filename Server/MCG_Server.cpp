#include <iostream>
#include <fstream>
#include <cstring>
#include <thread>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <atomic>
#include <ctime>
#include <mutex>
#include <sstream>

// Определяем платформу
#ifdef _WIN32
#define PLATFORM_WINDOWS 1
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

// Windows-specific
#define SHUT_RDWR SD_BOTH
#ifndef SOMAXCONN
#define SOMAXCONN 0x7fffffff
#endif
#else
#define PLATFORM_WINDOWS 0
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/types.h>

// POSIX constants
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

// Кроссплатформенные типы
#ifdef _WIN32
typedef SOCKET socket_t;
#define socket_close closesocket
#define socket_errno WSAGetLastError()
#else
typedef int socket_t;
#define socket_close close
#define socket_errno errno
#endif

using namespace std;

const int PORT = 8080;

// Типы действий
enum class ActionType {
    MOVE,
    ATTACK,
    DEFEND,
    USE_ITEM,
    WAIT,
    SKIP
};

// Структура игрока
struct Player {
    string name;
    int id;
    bool is_admin;
    bool is_ready = false;
    ActionType last_action = ActionType::WAIT;
    int hp = 100;
    int max_hp = 100;
    int level = 1;
    int exp = 0;
    int x = 0;
    int y = 0;
    int gold = 0;
};

// Состояние игры
struct GameState {
    bool is_active = false;
    time_t turn_start_time;
    int turn_duration_seconds = 1800; // 30 минут по умолчанию
    int current_turn = 1;
    vector<string> turn_log;
    map<int, Player> players; // ID -> Player
};

// Глобальные данные
vector<socket_t> clients;
atomic<int> client_count(0);
map<socket_t, pair<string, int>> client_info;
map<socket_t, bool> admin_clients; // Администраторы
atomic<int> next_client_id(1);
string Password = "null";
string Name = "Chat";
int max_clients = 10; // Значение по умолчанию
GameState game_state;
mutex game_mutex;

// Прототипы функций
bool network_init();
void network_cleanup();
socket_t socket_create();
int socket_bind(socket_t sock, const struct sockaddr* addr, socklen_t addrlen);
int socket_listen(socket_t sock, int backlog);
socket_t socket_accept(socket_t sock, struct sockaddr* addr, socklen_t* addrlen);
int socket_send(socket_t sock, const char* data, size_t length);
int socket_recv(socket_t sock, char* buffer, size_t buffer_size);
int socket_setopt(socket_t sock, int level, int optname, const void* optval, socklen_t optlen);
void broadcast_message(const string& message, socket_t sender);
void handle_client(socket_t client_sock);
void game_timer_thread();
void process_turn_end();
void process_game_command(socket_t client_sock, const string& command, int player_id, bool is_admin);
void send_time_remaining(socket_t client_sock);
void save_game_state(const string& filename);
void load_game_state(const string& filename);
void process_admin_command_save(const string& filename);
void process_admin_command_load(const string& filename);
void auto_save_thread();

// Инициализация сети
bool network_init() {
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        cerr << "WSAStartup failed" << endl;
        return false;
    }
#endif
    return true;
}

// Очистка сети
void network_cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// Создание сокета
socket_t socket_create() {
    return ::socket(AF_INET, SOCK_STREAM, 0);
}

// Привязка сокета к адресу
int socket_bind(socket_t sock, const struct sockaddr* addr, socklen_t addrlen) {
    return ::bind(sock, addr, addrlen);
}

// Начало прослушивания
int socket_listen(socket_t sock, int backlog) {
    return ::listen(sock, backlog);
}

// Принятие соединения
socket_t socket_accept(socket_t sock, struct sockaddr* addr, socklen_t* addrlen) {
    return ::accept(sock, addr, addrlen);
}

// Отправка данных
int socket_send(socket_t sock, const char* data, size_t length) {
#ifdef _WIN32
    return send(sock, data, static_cast<int>(length), 0);
#else
    return send(sock, data, length, 0);
#endif
}

// Получение данных
int socket_recv(socket_t sock, char* buffer, size_t buffer_size) {
#ifdef _WIN32
    return recv(sock, buffer, static_cast<int>(buffer_size), 0);
#else
    return recv(sock, buffer, buffer_size, 0);
#endif
}

// Установка опций сокета
int socket_setopt(socket_t sock, int level, int optname, const void* optval, socklen_t optlen) {
#ifdef _WIN32
    return setsockopt(sock, level, optname, reinterpret_cast<const char*>(optval), optlen);
#else
    return setsockopt(sock, level, optname, optval, optlen);
#endif
}

// Рассылка сообщения всем клиентам кроме отправителя
void broadcast_message(const string& message, socket_t sender) {
    // Создаем временную копию списка клиентов
    vector<socket_t> temp_clients;
    {
        static mutex clients_mutex;
        lock_guard<mutex> lock(clients_mutex);
        temp_clients = clients;
    }

    for (auto client : temp_clients) {
        if (client != sender) {
            socket_send(client, message.c_str(), message.length());
        }
    }
}

// Таймер для проверки окончания хода
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

        // Вызываем process_turn_end() вне блокировки мьютекса
        if (should_process_turn) {
            process_turn_end();
            broadcast_message("\n=== Turn automatically ended by timer ===\n", INVALID_SOCKET);
        }
    }
}

// Обработка окончания хода
void process_turn_end() {
    // Берем мьютекс для работы с игровым состоянием
    lock_guard<mutex> lock(game_mutex);

    // Проверяем, все ли игроки сделали ход
    bool all_ready = true;
    for (auto& pair : game_state.players) {
        auto& player = pair.second;
        if (!player.is_ready) {
            all_ready = false;
            break;
        }
    }

    // Логика игры
    string turn_summary = "\n=== Turn " + to_string(game_state.current_turn) + " Summary ===\n";

    // Обработка действий игроков
    for (auto& pair : game_state.players) {
        auto& p = pair.second;
        switch (p.last_action) {
        case ActionType::MOVE:
            turn_summary += p.name + " moved to position (" +
                to_string(p.x) + "," + to_string(p.y) + ")\n";
            break;
        case ActionType::ATTACK:
            // Логика атаки
            turn_summary += p.name + " attacked!\n";
            break;
        case ActionType::DEFEND:
            turn_summary += p.name + " is defending.\n";
            break;
        case ActionType::USE_ITEM:
            turn_summary += p.name + " used an item.\n";
            break;
        default:
            turn_summary += p.name + " waited.\n";
        }

        // Сбрасываем флаг готовности
        p.is_ready = false;
    }

    turn_summary += "=============================\n";

    // Сохраняем в лог
    game_state.turn_log.push_back(turn_summary);

    // Начинаем новый ход
    game_state.current_turn++;
    game_state.turn_start_time = time(nullptr);

    // Освобождаем мьютекс перед отправкой сообщений
    // (lock_guard автоматически освобождается при выходе из функции)
}

// В сервере, после старта игры
void send_time_remaining(socket_t client_sock) {
    if (!game_state.is_active) return;

    time_t current_time = time(nullptr);
    time_t elapsed = current_time - game_state.turn_start_time;
    time_t remaining = game_state.turn_duration_seconds - elapsed;

    if (remaining > 0) {
        int minutes = remaining / 60;
        int seconds = remaining % 60;
        string time_msg = "Time remaining: " + to_string(minutes) + "m " +
            to_string(seconds) + "s\n";
        socket_send(client_sock, time_msg.c_str(), time_msg.length());
    }
}

// Обработка игровых команд
void process_game_command(socket_t client_sock, const string& command, int player_id, bool is_admin) {
    // Пытаемся взять мьютекс с таймаутом
    if (!game_mutex.try_lock()) {
        socket_send(client_sock, "Server is busy processing other commands. Please try again.\n", 68);
        return;
    }

    // Используем unique_lock для автоматического освобождения
    unique_lock<mutex> lock(game_mutex, adopt_lock);

    if (!game_state.is_active && command != "/start_game" &&
        command.find("/set_") != 0 && command != "/status") {
        socket_send(client_sock, "Game is not active. Admin must start the game.\n", 52);
        return;
    }

    // Парсинг команды
    istringstream iss(command.substr(1));
    string cmd;
    iss >> cmd;

    if (cmd == "move") {
        string direction;
        iss >> direction;

        // Проверяем существование игрока
        if (game_state.players.find(player_id) == game_state.players.end()) {
            socket_send(client_sock, "Player not found!\n", 19);
            return;
        }

        auto& player = game_state.players[player_id];

        if (player.is_ready) {
            socket_send(client_sock, "You already made your move this turn!\n", 40);
            return;
        }

        // Логика движения
        if (direction == "north") player.y++;
        else if (direction == "south") player.y--;
        else if (direction == "east") player.x++;
        else if (direction == "west") player.x--;
        else {
            socket_send(client_sock, "Invalid direction. Use: north, south, east, west\n", 50);
            return;
        }

        player.last_action = ActionType::MOVE;
        player.is_ready = true;

        string response = "You moved " + direction + ". Position: (" +
            to_string(player.x) + "," + to_string(player.y) + ")\n";
        socket_send(client_sock, response.c_str(), response.length());

        // Сохраняем данные для отправки
        string broadcast_msg = player.name + " moved " + direction + ".";

        // Освобождаем мьютекс перед отправкой сообщения
        lock.unlock();
        broadcast_message(broadcast_msg, client_sock);

    }
    else if (cmd == "attack") {
        int target_id;
        iss >> target_id;

        // Проверяем существование атакующего
        if (game_state.players.find(player_id) == game_state.players.end()) {
            socket_send(client_sock, "Player not found!\n", 19);
            return;
        }

        if (game_state.players.find(target_id) == game_state.players.end()) {
            socket_send(client_sock, "Target player not found!\n", 27);
            return;
        }

        auto& attacker = game_state.players[player_id];
        auto& target = game_state.players[target_id];

        if (attacker.is_ready) {
            socket_send(client_sock, "You already made your move this turn!\n", 40);
            return;
        }

        // Простая логика атаки
        int damage = 10 + (attacker.level * 2);
        target.hp -= damage;
        if (target.hp < 0) target.hp = 0;

        attacker.last_action = ActionType::ATTACK;
        attacker.is_ready = true;
        attacker.exp += 5;

        // Проверка уровня
        bool level_up = false;
        if (attacker.exp >= 100) {
            attacker.level++;
            attacker.exp = 0;
            attacker.max_hp += 20;
            attacker.hp = attacker.max_hp;
            level_up = true;
        }

        string response = "You attacked player " + target.name + " for " +
            to_string(damage) + " damage!\n";
        if (target.hp <= 0) {
            response += target.name + " has been defeated!\n";
            // Награда за победу
            attacker.gold += 50;
            attacker.exp += 20;
            response += "You gained 50 gold and 20 XP!\n";
        }
        socket_send(client_sock, response.c_str(), response.length());

        // Сохраняем данные для отправки
        string broadcast_msg = attacker.name + " attacked " + target.name +
            " for " + to_string(damage) + " damage!";
        string level_up_msg = attacker.name + " reached level " + to_string(attacker.level) + "!";

        // Освобождаем мьютекс перед отправкой сообщения
        lock.unlock();

        // Отправляем сообщения
        broadcast_message(broadcast_msg, client_sock);
        if (level_up) {
            broadcast_message(level_up_msg, INVALID_SOCKET);
        }

    }
    else if (cmd == "defend") {
        if (game_state.players.find(player_id) == game_state.players.end()) {
            socket_send(client_sock, "Player not found!\n", 19);
            return;
        }

        auto& player = game_state.players[player_id];
        player.last_action = ActionType::DEFEND;
        player.is_ready = true;
        socket_send(client_sock, "You are defending this turn.\n", 30);

    }
    else if (cmd == "use") {
        string item;
        iss >> item;
        // Логика использования предмета
        socket_send(client_sock, "Item used!\n", 11);

    }
    else if (cmd == "skip") {
        if (game_state.players.find(player_id) == game_state.players.end()) {
            socket_send(client_sock, "Player not found!\n", 19);
            return;
        }

        auto& player = game_state.players[player_id];
        player.last_action = ActionType::SKIP;
        player.is_ready = true;
        socket_send(client_sock, "You skipped your turn.\n", 24);

    }
    else if (cmd == "ready") {
        if (game_state.players.find(player_id) == game_state.players.end()) {
            socket_send(client_sock, "Player not found!\n", 19);
            return;
        }

        auto& player = game_state.players[player_id];
        player.is_ready = true;
        socket_send(client_sock, "You are ready for this turn.\n", 30);

        // Проверяем, все ли готовы
        bool all_ready = true;
        for (auto& pair : game_state.players) {
            auto& p = pair.second;
            if (!p.is_ready) {
                all_ready = false;
                break;
            }
        }

        if (all_ready) {
            // Освобождаем мьютекс перед отправкой
            lock.unlock();
            broadcast_message("All players are ready! Ending turn...", INVALID_SOCKET);
            process_turn_end();
        }

    }
    else if (cmd == "unready") {
        if (game_state.players.find(player_id) == game_state.players.end()) {
            socket_send(client_sock, "Player not found!\n", 19);
            return;
        }

        auto& player = game_state.players[player_id];
        player.is_ready = false;
        socket_send(client_sock, "You are no longer ready.\n", 26);

    }
    else if (cmd == "status") {
        if (game_state.players.find(player_id) == game_state.players.end()) {
            socket_send(client_sock, "Player not found!\n", 19);
            return;
        }

        auto& player = game_state.players[player_id];
        string status = "\n=== Your Status ===\n";
        status += "Name: " + player.name + "\n";
        status += "HP: " + to_string(player.hp) + "/" + to_string(player.max_hp) + "\n";
        status += "Level: " + to_string(player.level) + "\n";
        status += "XP: " + to_string(player.exp) + "/100\n";
        status += "Gold: " + to_string(player.gold) + "\n";
        status += "Position: (" + to_string(player.x) + "," + to_string(player.y) + ")\n";
        status += "Ready: " + string(player.is_ready ? "Yes" : "No") + "\n";
        status += "==================\n";
        socket_send(client_sock, status.c_str(), status.length());

    }
    else if (cmd == "map") {
        // Генерация простой карты
        string map = "\n=== Game Map ===\n";
        for (int y = 5; y >= -5; y--) {
            for (int x = -5; x <= 5; x++) {
                bool has_player = false;
                for (auto& pair : game_state.players) {
                    auto& player = pair.second;
                    if (player.x == x && player.y == y) {
                        map += to_string(player.id);
                        has_player = true;
                        break;
                    }
                }
                if (!has_player) {
                    if (x == 0 && y == 0) map += "X"; // Центр
                    else map += ".";
                }
                map += " ";
            }
            map += "\n";
        }
        map += "================\n";
        socket_send(client_sock, map.c_str(), map.length());

    }
    else if (is_admin) {
        // Команды администратора
        if (cmd == "start_game") {
            game_state.is_active = true;
            game_state.turn_start_time = time(nullptr);
            game_state.current_turn = 1;

            string broadcast_msg = "=== GAME STARTED ===\nTurn duration: " +
                to_string(game_state.turn_duration_seconds / 60) + " minutes\n";

            lock.unlock();
            broadcast_message(broadcast_msg, INVALID_SOCKET);
            cout << "=== GAME STARTED ===\n";
        }
        else if (cmd == "pause_game") {
            game_state.is_active = false;

            lock.unlock();
            broadcast_message("Game paused by admin.", INVALID_SOCKET);

        }
        else if (cmd == "set_turn_time") {
            int seconds;
            iss >> seconds;
            game_state.turn_duration_seconds = seconds;

            string broadcast_msg = "Turn duration set to " + to_string(seconds / 60) + " minutes.";

            lock.unlock();
            broadcast_message(broadcast_msg, INVALID_SOCKET);

        }
        else if (cmd == "end_turn") {
            lock.unlock();
            process_turn_end();

        }
        else if (cmd == "add_item") {
            int target_id;
            string item;
            iss >> target_id >> item;
            // Логика добавления предмета
            socket_send(client_sock, "Item added.\n", 12);

        }
        else if (cmd == "set_hp") {
            int target_id, hp;
            iss >> target_id >> hp;
            if (game_state.players.find(target_id) != game_state.players.end()) {
                game_state.players[target_id].hp = hp;
                socket_send(client_sock, "HP set successfully.\n", 22);
            }
        }
    }
    else {
        socket_send(client_sock, "Unknown command. Type /help for available commands.\n", 55);
    }
}

// Сохранение состояния игры в файл (без ID)
void save_game_state(const string& filename) {
    ofstream file(filename);
    if (!file.is_open()) {
        cerr << "Failed to save game state to " << filename << endl;
        return;
    }

    lock_guard<mutex> lock(game_mutex);

    // Сохраняем общие параметры игры
    file << game_state.current_turn << endl;
    file << game_state.turn_duration_seconds << endl;
    file << game_state.is_active << endl;
    file << game_state.players.size() << endl;

    // Сохраняем данные игроков
    for (auto& pair : game_state.players) {
        auto& player = pair.second;
        file << player.name << " "
            << player.hp << " "
            << player.max_hp << " "
            << player.level << " "
            << player.exp << " "
            << player.x << " "
            << player.y << " "
            << player.gold << " "
            << player.is_admin << endl;
    }

    // Сохраняем лог последних ходов (опционально)
    int log_size = min(10, (int)game_state.turn_log.size());
    file << log_size << endl;
    for (int i = 0; i < log_size; i++) {
        file << game_state.turn_log[game_state.turn_log.size() - log_size + i] << "|||";
    }

    file.close();
    cout << "Game state saved to " << filename << endl;
}

// Загрузка состояния игры из файла
void load_game_state(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Failed to load game state from " << filename << endl;
        return;
    }

    lock_guard<mutex> lock(game_mutex);

    // Очищаем текущее состояние
    game_state.players.clear();
    game_state.turn_log.clear();
    next_client_id = 1;

    // Загружаем общие параметры
    file >> game_state.current_turn;
    file >> game_state.turn_duration_seconds;
    file >> game_state.is_active;

    int player_count;
    file >> player_count;

    // Загружаем игроков с новыми ID
    for (int i = 0; i < player_count; i++) {
        Player player;
        file >> player.name
            >> player.hp
            >> player.max_hp
            >> player.level
            >> player.exp
            >> player.x
            >> player.y
            >> player.gold
            >> player.is_admin;

        // Генерируем новый ID
        int new_id = next_client_id++;
        player.id = new_id;

        game_state.players[new_id] = player;

        cout << "Loaded player: " << player.name
            << " (HP: " << player.hp << "/" << player.max_hp
            << ", Level: " << player.level << ")" << endl;
    }

    // Загружаем лог ходов
    int log_size;
    file >> log_size;
    file.ignore(); // Пропускаем перевод строки

    for (int i = 0; i < log_size; i++) {
        string log_entry;
        getline(file, log_entry, '|'); // Читаем до разделителя

        if (!log_entry.empty()) {
            game_state.turn_log.push_back(log_entry);
        }

        // Пропускаем оставшиеся разделители
        file.ignore(2); // Пропускаем "||"
    }

    file.close();
    cout << "Game state loaded from " << filename << ". "
        << player_count << " players restored." << endl;
}

// Команда для сохранения игры
void process_admin_command_save(const string& filename) {
    save_game_state(filename);
    string msg = "[SERVER] Game saved to " + filename;
    broadcast_message(msg, INVALID_SOCKET);
}

// Команда для загрузки игры
void process_admin_command_load(const string& filename) {
    load_game_state(filename);
    string msg = "[SERVER] Game loaded from " + filename;
    broadcast_message(msg, INVALID_SOCKET);
}

// Автосохранение каждые 15 минут
void auto_save_thread() {
    while (true) {
        this_thread::sleep_for(chrono::minutes(15));

        if (game_state.is_active) {
            save_game_state("autosave.rpg");
            cout << "Auto-save completed" << endl;
        }
    }
}

// Обработчик клиента
void handle_client(socket_t client_sock) {
    char buffer[1024];

    cout << "Client connected: socket " << client_sock << endl;

    // Аутентификация
    bool authenticated = false;
    bool is_admin = false;
    string username;

    while (!authenticated) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = socket_recv(client_sock, buffer, sizeof(buffer) - 1);

        if (bytes <= 0) {
            cout << "Client disconnected during auth" << endl;
            socket_close(client_sock);
            client_count--;
            return;
        }

        buffer[bytes] = '\0';
        string packet(buffer);
        size_t pos1 = packet.find('|');

        if (pos1 != string::npos && packet.substr(0, pos1) == "AUTH") {
            string rest = packet.substr(pos1 + 1);
            size_t pos2 = rest.find('|');

            if (pos2 != string::npos) {
                username = rest.substr(0, pos2);
                string password = rest.substr(pos2 + 1);

                // Проверяем пароль администратора
                if (password == Password) {
                    is_admin = true;
                    admin_clients[client_sock] = true;
                    cout << "Admin user '" << username << "' connected" << endl;
                }

                // Регистрируем пользователя
                int client_id = next_client_id++;
                client_info[client_sock] = make_pair(username, client_id);
                authenticated = true;

                string auth_success = "OK|Welcome " + username;
                if (is_admin) {
                    auth_success += " (ADMIN)";
                }
                auth_success += "! Your ID: " + to_string(client_id) + "\n";
                auth_success += "Type /help for commands\n";
                socket_send(client_sock, auth_success.c_str(), auth_success.length());

                cout << "User '" << username << "' (ID: " << client_id << ") ";
                if (is_admin) cout << "[ADMIN] ";
                cout << "authenticated" << endl;

                // Уведомляем других
                string join_msg = "[SERVER] " + username;
                if (is_admin) join_msg += " [ADMIN]";
                join_msg += " joined the chat";
                broadcast_message(join_msg, client_sock);

                // Регистрируем игрока в игровой системе
                {
                    lock_guard<mutex> lock(game_mutex);
                    Player player;
                    player.name = username;
                    player.id = client_id;
                    player.is_admin = is_admin;

                    // Если игрок уже существует в сохраненной игре, загружаем его данные
                    bool player_exists = false;
                    for (auto& pair : game_state.players) {
                        auto& existing_player = pair.second;
                        if (existing_player.name == username) {
                            // Обновляем ID существующего игрока
                            existing_player.id = client_id;
                            player_exists = true;
                            break;
                        }
                    }

                    if (!player_exists) {
                        game_state.players[client_id] = player;
                    }
                }
            }
        }

        if (!authenticated) {
            string error = "ERROR|Invalid format. Use: AUTH|username|password\n";
            socket_send(client_sock, error.c_str(), error.length());
        }
    }

    // Основной цикл
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = socket_recv(client_sock, buffer, sizeof(buffer) - 1);

        if (bytes <= 0) {
            break;
        }

        buffer[bytes] = '\0';
        string message(buffer);

        if (!message.empty()) {
            // Проверка на команды
            if (message[0] == '/') {
                // Команда помощи
                if (message == "/help") {
                    string help_msg = "\n=== Available Commands ===\n";
                    help_msg += "/help - Show this message\n";
                    help_msg += "/list - List all connected clients\n";

                    // Проверяем, если это игровые команды
                    {
                        lock_guard<mutex> lock(game_mutex);
                        if (game_state.players.find(client_info[client_sock].second) != game_state.players.end()) {
                            help_msg += "/move [direction] - Move (north, south, east, west)\n";
                            help_msg += "/attack [target_id] - Attack another player\n";
                            help_msg += "/defend - Defend yourself\n";
                            help_msg += "/skip - Skip your turn\n";
                            help_msg += "/status - Check your status\n";
                            help_msg += "/map - Show game map\n";
                            help_msg += "/ready - Mark yourself as ready\n";
                            help_msg += "/unready - Mark yourself as not ready\n";
                        }
                    }

                    if (is_admin) {
                        help_msg += "/kick [id] - Kick a client\n";
                        help_msg += "/name [new_name] - Change server name\n";
                        help_msg += "/max [number] - Change max clients\n";
                        help_msg += "/info - Show server info\n";
                        help_msg += "/clients - Show detailed client info\n";
                        help_msg += "/start_game - Start the game\n";
                        help_msg += "/pause_game - Pause the game\n";
                        help_msg += "/set_turn_time [seconds] - Set turn duration\n";
                        help_msg += "/end_turn - Force end current turn\n";
                        help_msg += "/save [filename] - Save game state\n";
                        help_msg += "/load [filename] - Load game state\n";
                    }
                    help_msg += "========================\n";
                    socket_send(client_sock, help_msg.c_str(), help_msg.length());
                    continue;
                }

                // Команда списка клиентов
                else if (message == "/list") {
                    string list_msg = "\n=== Connected Clients (" + to_string(client_count) + ") ===\n";
                    for (auto& client : clients) {
                        if (client_info.find(client) != client_info.end()) {
                            auto& info = client_info[client];
                            list_msg += "ID: " + to_string(info.second) + " | Name: " + info.first;
                            if (admin_clients[client]) {
                                list_msg += " [ADMIN]";
                            }

                            // Добавляем игровую информацию
                            {
                                lock_guard<mutex> lock(game_mutex);
                                if (game_state.players.find(info.second) != game_state.players.end()) {
                                    auto& player = game_state.players[info.second];
                                    list_msg += " | HP: " + to_string(player.hp) + "/" + to_string(player.max_hp);
                                    list_msg += " | Level: " + to_string(player.level);
                                }
                            }
                            list_msg += "\n";
                        }
                    }
                    list_msg += "==============================\n";
                    socket_send(client_sock, list_msg.c_str(), list_msg.length());
                    continue;
                }

                // Команды администратора
                else if (is_admin) {
                    // Команда кика
                    if (message.length() > 6 && message.substr(0, 5) == "/kick") {
                        try {
                            int kick_id = stoi(message.substr(6));
                            bool found = false;

                            for (auto& client : clients) {
                                if (client_info.find(client) != client_info.end() &&
                                    client_info[client].second == kick_id) {

                                    // Не позволяем кикнуть другого админа
                                    if (!admin_clients[client]) {
                                        string kick_msg = "[SERVER] You have been kicked by admin";
                                        socket_send(client, kick_msg.c_str(), kick_msg.length());
                                        socket_close(client);

                                        string notify = "[SERVER] Client ID " + to_string(kick_id) +
                                            " was kicked by admin";
                                        broadcast_message(notify, INVALID_SOCKET);
                                        found = true;
                                        break;
                                    }
                                    else {
                                        string error = "ERROR|Cannot kick another admin\n";
                                        socket_send(client_sock, error.c_str(), error.length());
                                        found = true;
                                    }
                                }
                            }

                            if (!found) {
                                string error = "ERROR|Client not found\n";
                                socket_send(client_sock, error.c_str(), error.length());
                            }
                        }
                        catch (...) {
                            string error = "ERROR|Invalid client ID\n";
                            socket_send(client_sock, error.c_str(), error.length());
                        }
                        continue;
                    }

                    // Команда смены имени сервера
                    else if (message.length() > 6 && message.substr(0, 5) == "/name") {
                        Name = message.substr(6);
                        string notify = "[SERVER] Server name changed to: " + Name;
                        broadcast_message(notify, INVALID_SOCKET);
                        continue;
                    }

                    // Команда изменения лимита клиентов
                    else if (message.length() > 5 && message.substr(0, 4) == "/max") {
                        try {
                            int new_max = stoi(message.substr(5));
                            if (new_max > 0 && new_max < 1000) {
                                max_clients = new_max;
                                string notify = "[SERVER] Max clients changed to: " + to_string(max_clients);
                                broadcast_message(notify, INVALID_SOCKET);
                            }
                            else {
                                string error = "ERROR|Max clients must be between 1 and 1000\n";
                                socket_send(client_sock, error.c_str(), error.length());
                            }
                        }
                        catch (...) {
                            string error = "ERROR|Invalid number\n";
                            socket_send(client_sock, error.c_str(), error.length());
                        }
                        continue;
                    }

                    // Команда информации о сервере
                    else if (message == "/info") {
                        string info_msg = "\n=== Server Information ===\n";
                        info_msg += "Name: " + Name + "\n";
                        info_msg += "Port: " + to_string(PORT) + "\n";
                        info_msg += "Max clients: " + to_string(max_clients) + "\n";
                        info_msg += "Connected clients: " + to_string(client_count) + "\n";
                        info_msg += "Config password: " + Password + "\n";
                        info_msg += "Game active: " + string(game_state.is_active ? "Yes" : "No") + "\n";
                        info_msg += "Current turn: " + to_string(game_state.current_turn) + "\n";
                        info_msg += "Turn duration: " + to_string(game_state.turn_duration_seconds / 60) + " minutes\n";
                        info_msg += "==========================\n";
                        socket_send(client_sock, info_msg.c_str(), info_msg.length());
                        continue;
                    }

                    // Детальная информация о клиентах
                    else if (message == "/clients") {
                        string detailed_msg = "\n=== Detailed Client Information ===\n";
                        for (auto& client : clients) {
                            if (client_info.find(client) != client_info.end()) {
                                auto& info = client_info[client];
                                detailed_msg += "Socket: " + to_string(client) +
                                    " | ID: " + to_string(info.second) +
                                    " | Name: " + info.first;
                                if (admin_clients[client]) {
                                    detailed_msg += " [ADMIN]";
                                }
                                detailed_msg += "\n";
                            }
                        }
                        detailed_msg += "===================================\n";
                        socket_send(client_sock, detailed_msg.c_str(), detailed_msg.length());
                        continue;
                    }

                    // Игровые команды администратора
                    else if (message == "/start_game") {
                        game_state.is_active = true;
                        game_state.turn_start_time = time(nullptr);
                        game_state.current_turn = 1;
                        broadcast_message("=== GAME STARTED ===\nTurn duration: " +
                            to_string(game_state.turn_duration_seconds / 60) + " minutes\n", INVALID_SOCKET);
                        continue;
                    }
                    else if (message == "/pause_game") {
                        game_state.is_active = false;
                        broadcast_message("Game paused by admin.", INVALID_SOCKET);
                        continue;
                    }
                    else if (message.length() > 14 && message.substr(0, 14) == "/set_turn_time") {
                        try {
                            int seconds = stoi(message.substr(15));
                            game_state.turn_duration_seconds = seconds;
                            broadcast_message("Turn duration set to " + to_string(seconds / 60) + " minutes.", INVALID_SOCKET);
                        }
                        catch (...) {
                            string error = "ERROR|Invalid number\n";
                            socket_send(client_sock, error.c_str(), error.length());
                        }
                        continue;
                    }
                    else if (message == "/end_turn") {
                        process_turn_end();
                        continue;
                    }
                    else if (message.length() > 5 && message.substr(0, 5) == "/save") {
                        string filename = message.substr(6);
                        if (filename.empty()) filename = "game_save.rpg";
                        thread(save_game_state, filename).detach();
                        socket_send(client_sock, "Saving game...\n", 15);
                        continue;
                    }
                    else if (message.length() > 5 && message.substr(0, 5) == "/load") {
                        string filename = message.substr(6);
                        if (filename.empty()) filename = "game_save.rpg";
                        thread(load_game_state, filename).detach();
                        socket_send(client_sock, "Loading game...\n", 16);
                        continue;
                    }
                }

                // Игровые команды для всех игроков
                int player_id = client_info[client_sock].second;
                {
                    // Просто проверяем существование игрока
                    if (game_state.players.find(player_id) != game_state.players.end()) {
                        process_game_command(client_sock, message, player_id, is_admin);
                        continue;
                    }
                }

                // Неизвестная команда
                string error = "ERROR|Unknown command. Type /help for available commands\n";
                socket_send(client_sock, error.c_str(), error.length());
                continue;
            }

            // Обычное сообщение
            auto& info = client_info[client_sock];
            string full_msg = "[" + to_string(info.second) + "] " + info.first;
            if (is_admin) full_msg += " [ADMIN]";
            full_msg += ": " + message;

            cout << "Message: " << full_msg << endl;
            broadcast_message(full_msg, client_sock);
        }
    }

    // Клиент отключился
    auto it = find(clients.begin(), clients.end(), client_sock);
    if (it != clients.end()) {
        clients.erase(it);
        client_count--;
    }

    if (admin_clients.find(client_sock) != admin_clients.end()) {
        admin_clients.erase(client_sock);
    }

    {
        lock_guard<mutex> lock(game_mutex);
        int player_id = client_info[client_sock].second;
        if (game_state.players.find(player_id) != game_state.players.end()) {
            game_state.players.erase(player_id);
        }
    }

    if (client_info.find(client_sock) != client_info.end()) {
        string leave_msg = "[SERVER] " + client_info[client_sock].first + " left the chat";
        broadcast_message(leave_msg, INVALID_SOCKET);
        client_info.erase(client_sock);
    }

    socket_close(client_sock);
    cout << "Client disconnected. Total: " << client_count << endl;
}

int main() {
    cout << "=== Cross-platform RPG Game Server ===" << endl;

    if (!network_init()) {
        return 1;
    }

    cout << "Enter max clients: ";
    cin >> max_clients;
    cout << "Enter configuration password: ";
    cin >> Password;
    cout << "Enter server name: ";
    cin >> Name;

    // После ввода параметров, спросить о загрузке
    char load_choice;
    cout << "Load saved game? (y/n): ";
    cin >> load_choice;

    if (load_choice == 'y' || load_choice == 'Y') {
        string save_file;
        cout << "Enter save file name (default: game_save.rpg): ";
        cin >> save_file;
        if (save_file.empty()) save_file = "game_save.rpg";

        load_game_state(save_file);

        // Запускаем автосохранение после загрузки
        thread(auto_save_thread).detach();
    }

    cin.ignore();

    // Запускаем игровой таймер в отдельном потоке
    thread(game_timer_thread).detach();

    // Создаем сокет
    socket_t server_sock = socket_create();
    if (server_sock == INVALID_SOCKET) {
        cerr << "Socket creation failed: " << socket_errno << endl;
        network_cleanup();
        return 1;
    }

    // Настройка адреса
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Разрешаем повторное использование порта
    int opt = 1;
    if (socket_setopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == SOCKET_ERROR) {
        cerr << "Set socket option failed: " << socket_errno << endl;
        socket_close(server_sock);
        network_cleanup();
        return 1;
    }

    // Привязываем сокет
    if (socket_bind(server_sock, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Bind failed: " << socket_errno << endl;
        cerr << "Make sure:" << endl;
        cerr << "1. Port " << PORT << " is available" << endl;
        cerr << "2. No other server is running" << endl;
        cerr << "3. You have permission to bind to this port" << endl;
        socket_close(server_sock);
        network_cleanup();
        return 1;
    }

    // Начинаем прослушивание
    if (socket_listen(server_sock, 10) == SOCKET_ERROR) {
        cerr << "Listen failed: " << socket_errno << endl;
        socket_close(server_sock);
        network_cleanup();
        return 1;
    }

    // Выводим информацию о сервере
    cout << "\n=== Server Information ===" << endl;
    cout << "Name: " << Name << endl;
    cout << "Platform: ";
#ifdef _WIN32
    cout << "Windows" << endl;
#else
    cout << "Linux/Android" << endl;
#endif
    cout << "Port: " << PORT << endl;
    cout << "Max clients: " << max_clients << endl;
    cout << "Server IP: 0.0.0.0 (all interfaces)" << endl;
    cout << "Localhost: 127.0.0.1:" << PORT << endl;
    cout << "Game system: RPG Turn-based" << endl;
    cout << "Default turn time: " << game_state.turn_duration_seconds / 60 << " minutes" << endl;
    cout << "Waiting for connections..." << endl;
    cout << "Press Ctrl+C to stop server" << endl;
    cout << "==========================\n" << endl;

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        // Принимаем соединение
        socket_t client_sock = socket_accept(server_sock, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);

        if (client_sock == INVALID_SOCKET) {
            cerr << "Accept failed: " << socket_errno << endl;
            continue;
        }

        // Проверяем лимит клиентов
        if (client_count >= max_clients) {
            string error = "ERROR|Server is full. Max: " + to_string(max_clients);
            socket_send(client_sock, error.c_str(), error.length());
            socket_close(client_sock);
            cout << "Connection rejected: server full (" << max_clients << " clients)" << endl;
            continue;
        }

        // Сразу отправляем приветствие
        string welcome_msg = "=== Connected to " + Name + " RPG Server ===\n";
        welcome_msg += "Authentication required.\n";
        welcome_msg += "Format: AUTH|username|password\n";
        welcome_msg += "==================================\n";
        socket_send(client_sock, welcome_msg.c_str(), welcome_msg.length());

        // Добавляем клиента
        clients.push_back(client_sock);
        client_count++;

        // Выводим информацию о подключении
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        cout << "New connection from " << client_ip << ":" << ntohs(client_addr.sin_port) << endl;
        cout << "Total clients: " << client_count << "/" << max_clients << endl;

        // Запускаем обработчик в отдельном потоке
        thread client_thread(handle_client, client_sock);
        client_thread.detach();
    }

    // Закрываем серверный сокет (этот код никогда не выполнится в бесконечном цикле)
    socket_close(server_sock);
    network_cleanup();

    return 0;
}