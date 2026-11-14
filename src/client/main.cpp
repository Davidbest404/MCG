#include "client/network/connection.h"
#include "client/ui/console_ui.h"
#include "shared/util/logging.h"
#include <chrono>
#include <iostream>
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
    MCG_INFO("MCG client starting (Phase 4 — commands)");

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

        // Печатаем сообщения через ConsoleUI
        conn.set_message_callback(
            [&ui](mcg::protocol::MessageType type, nlohmann::json payload)
            {
                if (type == mcg::protocol::MessageType::Pong)
                {
                    ui.print_message("[System]: HP=" + std::to_string(payload.value("hp", 0)));
                }
                else if (type == mcg::protocol::MessageType::ChatMessage)
                {
                    std::string from = payload.value("from", "?");
                    std::string text = payload.value("text", "");
                    ui.print_message("[" + from + "]: " + text);
                }
            });

        conn.connect();
        conn.send(mcg::protocol::MessageType::Ping, {{"msg", "hello"}});

        // Главный цикл ввода
        while (true)
        {
            std::string line = ui.read_line("> ");

            if (line == "exit") break;
            if (line.empty()) continue;

            nlohmann::json msg = {{"text", line}};
            conn.send(mcg::protocol::MessageType::ChatMessage, msg);
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