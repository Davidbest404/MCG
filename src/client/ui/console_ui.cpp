#include "client/ui/console_ui.h"
#include "shared/color/tag_parser.h"
#include <iomanip>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace mcg::client
{
std::string colorize(const std::string& text)
{
    auto parsed = mcg::color::parse_colored(text);
    std::string out;
    const char* ansi_fg_16[] = {"\033[30m", "\033[34m", "\033[32m", "\033[36m",
                                "\033[31m", "\033[35m", "\033[33m", "\033[37m",
                                "\033[90m", "\033[94m", "\033[92m", "\033[96m",
                                "\033[91m", "\033[95m", "\033[93m", "\033[97m"};
    const char* ansi_bg_16[] = {"\033[40m",  "\033[44m",  "\033[42m",  "\033[46m",
                                "\033[41m",  "\033[45m",  "\033[43m",  "\033[47m",
                                "\033[100m", "\033[104m", "\033[102m", "\033[106m",
                                "\033[101m", "\033[105m", "\033[103m", "\033[107m"};

    for (const auto& seg : parsed.segments)
    {
        if (seg.fg_color != 0xFFFFFFFF)
        {
            if (seg.fg_color <= 15)
                out += ansi_fg_16[seg.fg_color];
            else
            {
                int r = (seg.fg_color >> 16) & 0xFF, g = (seg.fg_color >> 8) & 0xFF,
                    b = seg.fg_color & 0xFF;
                out += "\033[38;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" +
                       std::to_string(b) + "m";
            }
        }
        if (seg.bg_color != 0xFFFFFFFF)
        {
            if (seg.bg_color <= 15)
                out += ansi_bg_16[seg.bg_color];
            else
            {
                int r = (seg.bg_color >> 16) & 0xFF, g = (seg.bg_color >> 8) & 0xFF,
                    b = seg.bg_color & 0xFF;
                out += "\033[48;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" +
                       std::to_string(b) + "m";
            }
        }
        out += seg.text;
        if (seg.fg_color != 0xFFFFFFFF || seg.bg_color != 0xFFFFFFFF) out += "\033[0m";
    }
    return out;
}

ConsoleUI::ConsoleUI()
{
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
#else
    old_termios_ = new termios();
    tcgetattr(STDIN_FILENO, old_termios_);
    termios newt = *old_termios_;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
#endif
    std::cout << "\033[?25l\033[2J\033[H";
}

// ДОБАВЛЕНО: Реализация деструктора (исправляет ошибку LNK2019)
ConsoleUI::~ConsoleUI()
{
    std::cout << "\033[?25h"; // Возвращаем курсор
#ifndef _WIN32
    if (old_termios_)
    {
        tcsetattr(STDIN_FILENO, TCSANOW, old_termios_);
        delete old_termios_;
    }
#endif
}

void ConsoleUI::draw(const std::string& map_grid, const std::vector<std::string>& chat_history,
                     const std::string& input, bool game_mode)
{
    std::string buffer;
    buffer.reserve(8192);

    // ИСПРАВЛЕНО: полная очистка экрана каждый кадр (убирает шлейфы)
    buffer += "\033[H\033[J";

    // 1. Рисуем карту (первые 15 строк)
    buffer += "\033[1;1H"; // Курсор в левый верхний угол
    std::istringstream ss(map_grid);
    std::string line;
    int line_count = 0;
    while (std::getline(ss, line) && line_count < 15)
    {
        buffer += "\033[K" + colorize(line) + "\n";
        line_count++;
    }
    // Очищаем оставшиеся строки карты, если она меньше 15 строк
    while (line_count < 15)
    {
        buffer += "\033[K\n";
        line_count++;
    }

    // 2. Рисуем чат (начиная с 16 строки)
    buffer += "\033[16;1H";
    size_t start = chat_history.size() > 8 ? chat_history.size() - 8 : 0;
    for (size_t i = start; i < chat_history.size(); ++i)
    {
        buffer += "\033[K" + colorize(chat_history[i]) + "\n";
    }
    // Очищаем остатки чата
    for (size_t i = chat_history.size() - start; i < 8; ++i)
    {
        buffer += "\033[K\n";
    }

    // 3. Строка ввода (24 строка)
    buffer += "\033[24;1H";
    if (game_mode)
    {
        buffer += "\033[K\033[32m[WASD: Move, T: Chat]\033[0m ";
    }
    else
    {
        buffer += "\033[K> " + input;
    }

    std::cout << buffer << std::flush;
}

int ConsoleUI::read_key_non_blocking()
{
#ifdef _WIN32
    if (_kbhit()) return _getch();
#else
    char c;
    if (read(STDIN_FILENO, &c, 1) == 1) return c;
#endif
    return 0;
}

void ConsoleUI::flush_input_buffer()
{
#ifdef _WIN32
    FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
#else
    tcflush(STDIN_FILENO, TCIFLUSH);
#endif
}
} // namespace mcg::client