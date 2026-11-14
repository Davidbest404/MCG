#include "shared/util/logging.h"

int main(int /*argc*/, char** /*argv*/)
{
    mcg::log::init("mcg_server", "logs", spdlog::level::info);

    MCG_INFO("MCG server starting (Phase 0 — skeleton)");
    MCG_INFO("Hello from server!");

    MCG_INFO("MCG server shutting down");
    mcg::log::shutdown();
    return 0;
}