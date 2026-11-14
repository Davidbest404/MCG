#pragma once

#include <string>
#include <system_error>

namespace mcg::util
{

// Возвращает текст ошибки в UTF-8.
// На Windows конвертирует из ANSI (CP1251 и т.п.) в UTF-8,
// потому что ec.message() отдаёт системную кодировку.
std::string error_message(const std::error_code& ec);

} // namespace mcg::util