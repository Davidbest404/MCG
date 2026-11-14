#include "client/network/connection.h"
#include "client/ui/console_ui.h"
#include "shared/util/logging.h"
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    mcg::log::init("mcg_client", "logs", spdlog::level::info);

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

        // Общие данные для потоков
        std::mutex data_mutex;
        std::string current_map = "";
        std::vector<std::string> chat_history;
        std::string input_buffer = "";
        bool game_mode = true;
        bool running = true;

        // Коллбэк от сетевого потока (только обновляет данные, ничего не рисует!)
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
                }
                else if (type == mcg::protocol::MessageType::WorldSnapshot)
                {
                    auto grid = payload["grid"].get<std::vector<std::string>>();
                    current_map.clear();
                    for (const auto& row : grid)
                        current_map += row + "\n";
                }
            });

        conn.connect();
        conn.send(mcg::protocol::MessageType::Ping, {{"msg", "hello"}});
        conn.send(mcg::protocol::MessageType::ChatMessage, {{"text", "/map"}});

        // ГЛАВНЫЙ ИГРОВОЙ ЦИКЛ (Game Loop)
        while (running)
        {
            // 1. Обработка ввода
            int key = ui.read_key_non_blocking();
            if (key > 0)
            {
                if (key == 3)
                {
                    running = false;
                    break;
                } // Ctrl+C

                std::lock_guard<std::mutex> lock(data_mutex);
                if (game_mode)
                {
                    if (key == 'w' || key == 'W')
                        conn.send(mcg::protocol::MessageType::ChatMessage,
                                  {{"text", "/move north"}});
                    else if (key == 's' || key == 'S')
                        conn.send(mcg::protocol::MessageType::ChatMessage,
                                  {{"text", "/move south"}});
                    else if (key == 'a' || key == 'A')
                        conn.send(mcg::protocol::MessageType::ChatMessage,
                                  {{"text", "/move west"}});
                    else if (key == 'd' || key == 'D')
                        conn.send(mcg::protocol::MessageType::ChatMessage,
                                  {{"text", "/move east"}});
                    else if (key == ' ')
                        conn.send(mcg::protocol::MessageType::ChatMessage, {{"text", "/move up"}});
                    else if (key == 'T' || key == 't' || key == 13) // 13 = Enter
                    {
                        game_mode = false;
                        input_buffer.clear();
                    }
                }
                else
                {
                    if (key == 13) // Enter (отправка)
                    {
                        if (!input_buffer.empty())
                        {
                            conn.send(mcg::protocol::MessageType::ChatMessage,
                                      {{"text", input_buffer}});
                            input_buffer.clear();
                        }
                        game_mode = true;
                    }
                    else if (key == 27) // Esc (отмена)
                    {
                        input_buffer.clear();
                        game_mode = true;
                    }
                    else if (key == 8 || key == 127) // Backspace
                    {
                        if (!input_buffer.empty()) input_buffer.pop_back();
                    }
                    else if (key >= 32 && key <= 126) // Печатные символы
                    {
                        input_buffer.push_back(static_cast<char>(key));
                    }
                }
            }

            // 2. Отрисовка экрана
            {
                std::lock_guard<std::mutex> lock(data_mutex);
                ui.draw(current_map, chat_history, input_buffer, game_mode);
            }

            // 3. Ограничение FPS (~60 кадров в секунду)
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