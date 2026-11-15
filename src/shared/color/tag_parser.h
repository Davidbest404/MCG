#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mcg::color
{
struct StyledSegment
{
    std::string text;
    uint32_t fg_color = 0xFFFFFFFF; // 0xFFFFFFFF = default, 0-15 = ANSI, >15 = RGB
    uint32_t bg_color = 0xFFFFFFFF;
};

struct ParseResult
{
    std::vector<StyledSegment> segments;
    bool had_errors = false;
    std::vector<std::string> errors;
};

ParseResult parse_colored(std::string_view input);
std::string to_tagged_string(const ParseResult& parsed);
} // namespace mcg::color