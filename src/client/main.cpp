#include "shared/util/logging.h"

int main(int /*argc*/, char** /*argv*/)
{
    mcg::log::init("mcg_client", "logs", spdlog::level::info);

    MCG_INFO("MCG client starting (Phase 0 — skeleton)");
    MCG_INFO("Hello from client!");

    MCG_INFO("MCG client shutting down");
    mcg::log::shutdown();
    return 0;
}