#include "shared/util/logging.h"

#include <filesystem>
#include <memory>
#include <vector>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace mcg::log
{

void init(std::string_view app_name,
          std::string_view log_dir,
          spdlog::level::level_enum level)
{
    namespace fs = std::filesystem;

    const fs::path dir{log_dir};
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec)
    {
        spdlog::warn("Failed to create log directory '{}': {}", log_dir, ec.message());
    }

    std::vector<spdlog::sink_ptr> sinks;

    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console->set_level(level);
    sinks.push_back(console);

    const fs::path file_path = dir / (std::string{app_name} + ".log");
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        file_path.string(), 5 * 1024 * 1024, 3);
    file_sink->set_level(spdlog::level::trace);
    sinks.push_back(file_sink);

    auto logger = std::make_shared<spdlog::logger>(
        std::string{app_name}, sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::warn);

    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [t:%t] %v");

    MCG_INFO("Logging initialized (app='{}', dir='{}')", app_name, log_dir);
}

void shutdown()
{
    spdlog::shutdown();
}

} // namespace mcg::log