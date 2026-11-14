#pragma once

#include <string_view>

#include <spdlog/spdlog.h>

namespace mcg::log
{

// Initialize the global logger.
//   app_name — используется как имя файла лога.
//   log_dir  — директория для логов (создаётся автоматически).
//   level    — минимальный уровень для консольного вывода.
void init(std::string_view app_name,
          std::string_view log_dir,
          spdlog::level::level_enum level = spdlog::level::info);

void shutdown();

} // namespace mcg::log

// Удобные макросы для единообразного логирования по всему проекту.
#define MCG_TRACE(...)    ::spdlog::trace(__VA_ARGS__)
#define MCG_DEBUG(...)    ::spdlog::debug(__VA_ARGS__)
#define MCG_INFO(...)     ::spdlog::info(__VA_ARGS__)
#define MCG_WARN(...)     ::spdlog::warn(__VA_ARGS__)
#define MCG_ERROR(...)    ::spdlog::error(__VA_ARGS__)
#define MCG_CRITICAL(...) ::spdlog::critical(__VA_ARGS__)