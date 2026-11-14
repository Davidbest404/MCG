#include "client/ui/console_ui.h"
#include "shared/color/tag_parser.h"
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

// Преобразование тегов [cX] в ANSI-коды
std::string colorize(const std::string& text)
{
    auto parsed = mcg::color::parse_colored(text);
    std::string out;
    const char* ansi_colors[] = {"\033[30m", "\033[34m", "\033[32m", "\033[36m",
                                 "\033[31m", "\033[35m", "\033[33m", "\033[37m",
                                 "\033[90m", "\033[94m", "\033[92m", "\033[96m",
                                 "\033[91m", "\033[95m", "\033[93m", "\033[97m"};

    for (const auto& seg : parsed.segments)
    {
        if (seg.fg_color != -1)
            out += ansi_colors[seg.fg_color];
        else
            out += "\033[0m";
        out += seg.text;
        out += "\033[0m";
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
    // Скрываем курсор терминала (чтобы он не мигал при перерисовке)
    std::cout << "\033[?25l\033[2J\033[H";
}

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
    // Встаем в левый верхний угол
    std::cout << "\033[H";

    // 1. Рисуем карту
    std::istringstream ss(map_grid);
    std::string line;
    while (std::getline(ss, line))
    {
        std::cout << "\033[K" << colorize(line) << "\n";
    }

    std::cout << "\033[K\n"; // Пустая строка между картой и чатом

    // 2. Рисуем чат (последние 10 сообщений)
    size_t start = chat_history.size() > 10 ? chat_history.size() - 10 : 0;
    for (size_t i = start; i < chat_history.size(); ++i)
    {
        std::cout << "\033[K" << colorize(chat_history[i]) << "\n";
    }

    // 3. Рисуем строку ввода
    if (game_mode)
    {
        std::cout << "\033[K\033[32m[WASD: Move, Space: Up, Shift: Down, T: Chat]\033[0m "
                  << std::flush;
    }
    else
    {
        std::cout << "\033[K> " << input << std::flush;
    }
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

} // namespace mcg::client