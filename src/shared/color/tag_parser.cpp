#include "shared/color/tag_parser.h"

namespace mcg::color
{

namespace
{

bool parse_hex_digit(char c, int& out)
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

// ¬озвращает:
//   0 Ч это не тег (обычный символ '[')
//   1 Ч открывающий [cX]
//   2 Ч открывающий [bgX]
//   3 Ч закрывающий [/c] или [/cX]
//   4 Ч закрывающий [/bg] или [/bgX]
// tag_len Ч сколько символов занимает тег.
// color   Ч hex-значение (0..15) или -1, если закрывающий без значени€.
int detect_tag(std::string_view s, std::size_t pos, std::size_t& tag_len, int& color)
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
    {
        return 0;
    }

    int hex_val = -1;
    bool has_hex = false;
    if (i < s.size())
    {
        int tmp = 0;
        if (parse_hex_digit(s[i], tmp))
        {
            has_hex = true;
            hex_val = tmp;
            ++i;
        }
    }

    if (i >= s.size() || s[i] != ']') return 0;

    // ќткрывающий тег без hex-значени€ Ч не тег.
    if (!closing && !has_hex) return 0;

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
        int fg = -1;
        int bg = -1;
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
            int color = -1;
            const int kind = detect_tag(input, i, tag_len, color);
            if (kind == 0)
            {
                buffer.push_back(input[i]);
                ++i;
                continue;
            }

            flush();
            switch (kind)
            {
            case 1: // [cX]
                stack.push_back(current);
                current.fg = color;
                break;
            case 2: // [bgX]
                stack.push_back(current);
                current.bg = color;
                break;
            case 3: // [/c] или [/cX]
                if (!stack.empty())
                {
                    current = stack.back();
                    stack.pop_back();
                }
                else
                {
                    result.had_errors = true;
                    result.errors.emplace_back("Unmatched [/c]");
                }
                break;
            case 4: // [/bg] или [/bgX]
                if (!stack.empty())
                {
                    current = stack.back();
                    stack.pop_back();
                }
                else
                {
                    result.had_errors = true;
                    result.errors.emplace_back("Unmatched [/bg]");
                }
                break;
            default:
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

    if (!stack.empty())
    {
        result.had_errors = true;
        result.errors.emplace_back("Unclosed tags at end of input");
    }

    return result;
}

std::string to_tagged_string(const ParseResult& parsed)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    int cur_fg = -1;
    int cur_bg = -1;

    for (const auto& seg : parsed.segments)
    {
        if (seg.fg_color != cur_fg)
        {
            if (cur_fg != -1) out += "[/c]";
            if (seg.fg_color != -1)
            {
                out += "[c";
                out += kHex[seg.fg_color];
                out += "]";
            }
            cur_fg = seg.fg_color;
        }
        if (seg.bg_color != cur_bg)
        {
            if (cur_bg != -1) out += "[/bg]";
            if (seg.bg_color != -1)
            {
                out += "[bg";
                out += kHex[seg.bg_color];
                out += "]";
            }
            cur_bg = seg.bg_color;
        }
        out += seg.text;
    }

    if (cur_fg != -1) out += "[/c]";
    if (cur_bg != -1) out += "[/bg]";

    return out;
}

} // namespace mcg::color