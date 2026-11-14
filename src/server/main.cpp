#include "server/core/server.h"
#include "shared/util/logging.h"
#include <iostream>
#include <random>
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
    MCG_INFO("MCG server starting (Phase 6 — UI & Procedural World)");

    uint16_t port = 0;
    uint32_t seed = 0;
    bool fog_enabled = true;
    bool port_provided = false;
    bool seed_provided = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if ((arg == "--port" || arg == "-p") && i + 1 < argc)
        {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
            port_provided = true;
        }
        else if (arg == "--seed" && i + 1 < argc)
        {
            seed = static_cast<uint32_t>(std::stoul(argv[++i]));
            seed_provided = true;
        }
        else if (arg == "--no-fog")
        {
            fog_enabled = false;
        }
    }

    if (!port_provided)
    {
        std::cout << "Enter port to listen on (default 7777, 0 for any free port): ";
        std::string input;
        std::getline(std::cin, input);
        if (!input.empty())
        {
            try
            {
                port = static_cast<uint16_t>(std::stoi(input));
            }
            catch (...)
            {
                port = 7777;
            }
        }
        else
        {
            port = 7777;
        }
    }

    if (!seed_provided)
    {
        std::cout << "Enter world seed (default random): ";
        std::string input;
        std::getline(std::cin, input);
        if (!input.empty())
        {
            try
            {
                seed = static_cast<uint32_t>(std::stoul(input));
            }
            catch (...)
            {
                seed = 0;
            }
        }
    }

    if (seed == 0)
    {
        seed = static_cast<uint32_t>(std::random_device{}());
        MCG_INFO("No seed provided, generated random seed: {}", seed);
    }
    else
    {
        MCG_INFO("Using provided seed: {}", seed);
    }

    try
    {
        mcg::server::Server server(port, seed, fog_enabled);
        server.run();
    }
    catch (const std::exception& e)
    {
        MCG_ERROR("Server error: {}", e.what());
    }

    mcg::log::shutdown();
    return 0;
}