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
    MCG_INFO("MCG client starting (Phase 2 — network)");

    std::string host = "127.0.0.1";
    uint16_t port = 0;
    bool port_provided = false;

    // Проверяем аргументы командной строки
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if ((arg == "--host" || arg == "-h") && i + 1 < argc)
        {
            host = argv[++i];
        }
        else if ((arg == "--port" || arg == "-p") && i + 1 < argc)
        {
            try
            {
                int val = std::stoi(argv[++i]);
                if (val < 0 || val > 65535) throw std::out_of_range("port out of range");
                port = static_cast<uint16_t>(val);
                port_provided = true;
            }
            catch (...)
            {
                MCG_ERROR("Invalid port argument");
                return 1;
            }
        }
    }

    // Интерактивный ввод, если порт не передан
    if (!port_provided)
    {
        std::cout << "Enter server port (default 7777): ";
        std::string input;
        std::getline(std::cin, input);
        if (!input.empty())
        {
            try
            {
                int val = std::stoi(input);
                if (val < 0 || val > 65535) throw std::out_of_range("port out of range");
                port = static_cast<uint16_t>(val);
            }
            catch (...)
            {
                MCG_WARN("Invalid input, defaulting to 7777");
                port = 7777;
            }
        }
        else
        {
            port = 7777;
        }
    }

    MCG_INFO("Connecting to {}:{}", host, port);

    try
    {
        mcg::client::Connection conn(host, port);

        conn.set_message_callback(
            [](mcg::protocol::MessageType type, nlohmann::json payload)
            {
                if (type == mcg::protocol::MessageType::Pong)
                {
                    MCG_INFO("Received Pong: {}", payload.dump());
                }
            });

        conn.connect();

        MCG_INFO("Sending Ping...");
        conn.send(mcg::protocol::MessageType::Ping, {{"msg", "hello"}});

        std::this_thread::sleep_for(std::chrono::seconds(2));
        conn.disconnect();
    }
    catch (const std::exception& e)
    {
        MCG_ERROR("Client error: {}", e.what());
    }

    mcg::log::shutdown();
    return 0;
}