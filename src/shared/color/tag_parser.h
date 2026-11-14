#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mcg::color
{

// ќдин фрагмент текста с одинаковым цветом.
struct StyledSegment
{
    std::string text;
    int fg_color = -1; // -1 = default, 0..15 = палитра
    int bg_color = -1;
};

struct ParseResult
{
    std::vector<StyledSegment> segments;
    bool had_errors = false;
    std::vector<std::string> errors;
};

// ѕарсит строку с тегами [cX]...[/cX] и [bgX]...[/bgX].
// X Ч hex 0-F. ¬ложенность поддерживаетс€ (стек).
// Ќезакрытые теги / лишние закрывающие Ч попадают в errors, но
// парсер не падает: текст после них идЄт с текущим цветом.
ParseResult parse_colored(std::string_view input);

// ќбратное преобразование: собирает строку с тегами из ParseResult.
std::string to_tagged_string(const ParseResult& parsed);

} // namespace mcg::color