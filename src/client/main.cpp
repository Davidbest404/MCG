#include "client/network/connection.h"
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
        mcg::client::Connection conn(host, port);

        conn.set_message_callback(
            [](mcg::protocol::MessageType type, nlohmann::json payload)
            {
                if (type == mcg::protocol::MessageType::Pong)
                {
                    MCG_INFO("Received Pong: HP={}", payload.value("hp", 0));
                }
                else if (type == mcg::protocol::MessageType::ChatMessage)
                {
                    std::string from = payload.value("from", "?");
                    std::string text = payload.value("text", "");
                    std::cout << "[" << from << "]: " << text << std::endl;
                }
            });

        conn.connect();
        conn.send(mcg::protocol::MessageType::Ping, {{"msg", "hello"}});

        std::cout << "Connected! Type messages or /commands. Ctrl+C to exit." << std::endl;

        // Читаем ввод пользователя
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (line.empty()) continue;

            nlohmann::json msg = {{"text", line}};
            conn.send(mcg::protocol::MessageType::ChatMessage, msg);
        }
    }
    catch (const std::exception& e)
    {
        MCG_ERROR("Client error: {}", e.what());
    }

    mcg::log::shutdown();
    return 0;
}