#include "ColorParser.h"
#include "ConsoleHelper.h"
#include <iostream>
#include <vector>
#include <cctype>

int hex_char_to_int(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

void print_colored_text(const std::string& text) {
    std::vector<int> text_stack = { 7 };
    std::vector<int> bg_stack = { 0 };
    size_t i = 0, len = text.length();

    auto apply = [&]() {
        ConsoleHelper::SetColor(text_stack.back(), bg_stack.back());
        };

    while (i < len) {
        if (text[i] == '[' && i + 3 < len && text[i + 1] == 'c' && isxdigit(text[i + 2]) && text[i + 3] == ']') {
            int color = hex_char_to_int(text[i + 2]);
            if (color != -1) {
                text_stack.push_back(color);
                apply();
                i += 4;
                continue;
            }
        }
        else if (text[i] == '[' && i + 4 < len && text[i + 1] == '/' && text[i + 2] == 'c' && isxdigit(text[i + 3]) && text[i + 4] == ']') {
            if (text_stack.size() > 1) {
                text_stack.pop_back();
                apply();
            }
            i += 5;
            continue;
        }
        else if (text[i] == '[' && i + 4 < len && text[i + 1] == 'b' && text[i + 2] == 'g' && isxdigit(text[i + 3]) && text[i + 4] == ']') {
            int color = hex_char_to_int(text[i + 3]);
            if (color != -1) {
                bg_stack.push_back(color);
                apply();
                i += 5;
                continue;
            }
        }
        else if (text[i] == '[' && i + 5 < len && text[i + 1] == '/' && text[i + 2] == 'b' && text[i + 3] == 'g' && isxdigit(text[i + 4]) && text[i + 5] == ']') {
            if (bg_stack.size() > 1) {
                bg_stack.pop_back();
                apply();
            }
            i += 6;
            continue;
        }
        apply();
        std::cout << text[i];
        i++;
    }
    ConsoleHelper::ResetColor();
}