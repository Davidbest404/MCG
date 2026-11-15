#include "shared/color/tag_parser.h"

namespace mcg::color
{

namespace
{
bool parse_hex(char c, int& out)
{
    if (c >= '0' && c <= '9')
    {
        out = c - '0';
        return true;
    }
    if (c >= 'a' && c <= 'f')
    {
        out = 10 + (c - 'a');
        return true;
    }
    if (c >= 'A' && c <= 'F')
    {
        out = 10 + (c - 'A');
        return true;
    }
    return false;
}

// 0 - not tag, 1 - fg, 2 - bg, 3 - /fg, 4 - /bg
int detect_tag(std::string_view s, std::size_t pos, std::size_t& tag_len, uint32_t& color)
{
    if (pos >= s.size() || s[pos] != '[') return 0;
    std::size_t i = pos + 1;
    bool closing = false;
    if (i < s.size() && s[i] == '/')
    {
        closing = true;
        ++i;
    }

    bool is_bg = false;
    if (i + 1 < s.size() && s[i] == 'b' && s[i + 1] == 'g')
    {
        is_bg = true;
        i += 2;
    }
    else if (i < s.size() && (s[i] == 'c' || s[i] == 'C'))
    {
        ++i;
    }
    else
        return 0;

    uint32_t hex_val = 0xFFFFFFFF;
    int digits = 0;
    while (i < s.size() && s[i] != ']')
    {
        int val;
        if (!parse_hex(s[i], val)) return 0;
        if (digits == 0)
            hex_val = val;
        else
            hex_val = (hex_val << 4) | val;
        digits++;
        i++;
    }

    if (i >= s.size() || s[i] != ']') return 0;
    if (!closing && digits != 1 && digits != 6) return 0; // Только 1 или 6 символов

    tag_len = (i + 1) - pos;
    color = hex_val;

    if (!closing) return is_bg ? 2 : 1;
    return is_bg ? 4 : 3;
}
} // namespace

ParseResult parse_colored(std::string_view input)
{
    ParseResult result;
    struct State
    {
        uint32_t fg = 0xFFFFFFFF, bg = 0xFFFFFFFF;
    };
    State current;
    std::vector<State> stack;
    std::string buffer;

    auto flush = [&]()
    {
        if (!buffer.empty())
        {
            result.segments.push_back({std::move(buffer), current.fg, current.bg});
            buffer.clear();
        }
    };

    std::size_t i = 0;
    while (i < input.size())
    {
        if (input[i] == '[')
        {
            std::size_t tag_len = 0;
            uint32_t color = 0xFFFFFFFF;
            int kind = detect_tag(input, i, tag_len, color);
            if (kind == 0)
            {
                buffer.push_back(input[i]);
                ++i;
                continue;
            }

            flush();
            switch (kind)
            {
            case 1:
                stack.push_back(current);
                current.fg = color;
                break;
            case 2:
                stack.push_back(current);
                current.bg = color;
                break;
            case 3:
                if (!stack.empty())
                {
                    current = stack.back();
                    stack.pop_back();
                }
                break;
            case 4:
                if (!stack.empty())
                {
                    current = stack.back();
                    stack.pop_back();
                }
                break;
            }
            i += tag_len;
        }
        else
        {
            buffer.push_back(input[i]);
            ++i;
        }
    }
    flush();
    return result;
}

std::string to_tagged_string(const ParseResult& parsed)
{
    // Заглушка для обратной совместимости
    std::string out;
    for (const auto& seg : parsed.segments)
        out += seg.text;
    return out;
}

} // namespace mcg::color