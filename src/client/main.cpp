#include "client/network/connection.h"
#include "client/ui/console_ui.h"
#include "shared/util/logging.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#endif

static std::atomic<bool> g_running{true};

int main(int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    mcg::log::init("mcg_client", "logs", spdlog::level::info);
    MCG_INFO("MCG client starting");

    std::string host = "127.0.0.1";
    uint16_t port = 0;
    bool port_provided = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if ((arg == "--host" || arg == "-h") && i + 1 < argc)
            host = argv[++i];
        else if ((arg == "--port" || arg == "-p") && i + 1 < argc)
        {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
            port_provided = true;
        }
    }

    if (!port_provided)
    {
        std::cout << "Enter server port (default 7777): ";
        std::string input;
        std::getline(std::cin, input);
        port = input.empty() ? 7777 : static_cast<uint16_t>(std::stoi(input));
    }

    try
    {
        mcg::client::ConsoleUI ui;
        mcg::client::Connection conn(host, port);

        std::mutex data_mutex;
        std::string current_map = "";
        std::vector<std::string> chat_history;
        std::string input_buffer = "";
        bool game_mode = true;
        std::atomic<bool> dirty{true};

        conn.set_message_callback(
            [&](mcg::protocol::MessageType type, nlohmann::json payload)
            {
                std::lock_guard<std::mutex> lock(data_mutex);
                if (type == mcg::protocol::MessageType::ChatMessage)
                {
                    std::string from = payload.value("from", "?");
                    std::string text = payload.value("text", "");
                    std::string prefix = (from == "System") ? "[cE]" : "[cA]";
                    chat_history.push_back(prefix + "[" + from + "]: [c7]" + text);
                    if (chat_history.size() > 50) chat_history.erase(chat_history.begin());
                    dirty = true;
                }
                else if (type == mcg::protocol::MessageType::WorldSnapshot)
                {
                    auto grid = payload["grid"].get<std::vector<std::string>>();
                    current_map.clear();
                    for (const auto& row : grid)
                        current_map += row + "\n";
                    dirty = true;
                }
            });

        conn.connect();
        conn.send(mcg::protocol::MessageType::Ping, {{"msg", "hello"}});
        conn.send(mcg::protocol::MessageType::ChatMessage, {{"text", "/map"}});

#ifdef _WIN32
        bool prev_w = false, prev_a = false, prev_s = false, prev_d = false;
        bool prev_space = false, prev_shift = false, prev_enter = false, prev_esc = false;

        auto press = [](int vk, bool& prev) -> bool
        {
            bool curr = (GetAsyncKeyState(vk) & 0x8000) != 0;
            bool result = curr && !prev;
            prev = curr;
            return result;
        };
#endif

        while (g_running)
        {
            if (game_mode)
            {
#ifdef _WIN32
                // === ИГРОВОЙ РЕЖИМ (Windows) ===
                if (press('W', prev_w))
                    conn.send(mcg::protocol::MessageType::ChatMessage, {{"text", "/move north"}});
                if (press('S', prev_s))
                    conn.send(mcg::protocol::MessageType::ChatMessage, {{"text", "/move south"}});
                if (press('A', prev_a))
                    conn.send(mcg::protocol::MessageType::ChatMessage, {{"text", "/move west"}});
                if (press('D', prev_d))
                    conn.send(mcg::protocol::MessageType::ChatMessage, {{"text", "/move east"}});
                if (press(VK_SPACE, prev_space))
                    conn.send(mcg::protocol::MessageType::ChatMessage, {{"text", "/move up"}});
                if (press(VK_SHIFT, prev_shift))
                    conn.send(mcg::protocol::MessageType::ChatMessage, {{"text", "/move down"}});

                if (press(VK_RETURN, prev_enter))
                {
                    game_mode = false;
                    input_buffer.clear();
                    ui.flush_input_buffer(); // Очищаем буфер после нажатий WASD
                    dirty = true;
                }

                // ВАЖНО: в игре мы полностью очищаем входной буфер, чтобы клавиши не копились
                while (_kbhit())
                    _getch();

#else
                // === ИГРОВОЙ РЕЖИМ (Linux/macOS) ===
                int key = ui.read_key_non_blocking();
                if (key == 'w' || key == 'W')
                    conn.send(mcg::protocol::MessageType::ChatMessage, {{"text", "/move north"}});
                else if (key == 's' || key == 'S')
                    conn.send(mcg::protocol::MessageType::ChatMessage, {{"text", "/move south"}});
                else if (key == 'a' || key == 'A')
                    conn.send(mcg::protocol::MessageType::ChatMessage, {{"text", "/move west"}});
                else if (key == 'd' || key == 'D')
                    conn.send(mcg::protocol::MessageType::ChatMessage, {{"text", "/move east"}});
                else if (key == ' ')
                    conn.send(mcg::protocol::MessageType::ChatMessage, {{"text", "/move up"}});
                else if (key == 'z' || key == 'Z')
                    conn.send(mcg::protocol::MessageType::ChatMessage, {{"text", "/move down"}});
                else if (key == 13 || key == 10)
                {
                    game_mode = false;
                    input_buffer.clear();
                    ui.flush_input_buffer();
                    dirty = true;
                }
#endif
            }
            else
            {
                // === РЕЖИМ ЧАТА ===
                int key = ui.read_key_non_blocking();
                if (key > 0)
                {
                    if (key == 3)
                    {
                        g_running = false;
                        break;
                    }

                    if (key == 13 || key == 10)
                    {
                        // Отправка сообщения и возврат в игру
                        if (!input_buffer.empty())
                        {
                            conn.send(mcg::protocol::MessageType::ChatMessage,
                                      {{"text", input_buffer}});
                            input_buffer.clear();
                        }
                        game_mode = true;
#ifdef _WIN32
                        prev_enter = true; // Защита от повторного срабатывания
#endif
                    }
                    else if (key == 27)
                    {
                        input_buffer.clear();
                        game_mode = true;
                    }
                    else if (key == 8 || key == 127)
                    {
                        if (!input_buffer.empty()) input_buffer.pop_back();
                    }
                    else if (key >= 32)
                    {
                        input_buffer.push_back(static_cast<char>(key));
                    }
                    dirty = true;
                }
            }

            if (dirty)
            {
                std::string map_copy;
                std::vector<std::string> chat_copy;
                std::string input_copy;
                bool mode_copy;
                {
                    std::lock_guard<std::mutex> lock(data_mutex);
                    map_copy = current_map;
                    chat_copy = chat_history;
                    input_copy = input_buffer;
                    mode_copy = game_mode;
                    dirty = false;
                }
                ui.draw(map_copy, chat_copy, input_copy, mode_copy);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        conn.disconnect();
    }
    catch (const std::exception& e)
    {
        MCG_ERROR("Client error: {}", e.what());
    }

    mcg::log::shutdown();
    return 0;
}