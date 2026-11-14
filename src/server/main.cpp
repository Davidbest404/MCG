#include "server/core/server.h"
#include "shared/util/logging.h"
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    mcg::log::init("mcg_server", "logs", spdlog::level::info);
    MCG_INFO("MCG server starting (Phase 2 — network)");

    uint16_t port = 0;
    bool port_provided = false;

    // Проверяем аргументы командной строки
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if ((arg == "--port" || arg == "-p") && i + 1 < argc)
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
        std::cout << "Enter port to listen on (default 7777, 0 for any free port): ";
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

    try
    {
        mcg::server::Server server(port);
        server.run();
    }
    catch (const std::exception& e)
    {
        MCG_ERROR("Server error: {}", e.what());
    }

    mcg::log::shutdown();
    return 0;
}