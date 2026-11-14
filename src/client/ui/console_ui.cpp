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
std::string colorize(const std::string& text)
{
    auto parsed = mcg::color::parse_colored(text);
    std::string out;
    const char* ansi_fg[] = {"\033[30m", "\033[34m", "\033[32m", "\033[36m", "\033[31m", "\033[35m",
                             "\033[33m", "\033[37m", "\033[90m", "\033[94m", "\033[92m", "\033[96m",
                             "\033[91m", "\033[95m", "\033[93m", "\033[97m"};
    const char* ansi_bg[] = {"\033[40m",  "\033[44m",  "\033[42m",  "\033[46m",
                             "\033[41m",  "\033[45m",  "\033[43m",  "\033[47m",
                             "\033[100m", "\033[104m", "\033[102m", "\033[106m",
                             "\033[101m", "\033[105m", "\033[103m", "\033[107m"};

    for (const auto& seg : parsed.segments)
    {
        if (seg.fg_color != -1) out += ansi_fg[seg.fg_color];
        if (seg.bg_color != -1) out += ansi_bg[seg.bg_color];
        out += seg.text;
        out += "\033[0m"; // —брос всех цветов после сегмента
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

ConsoleUI::~ConsoleUI()
{
    std::cout << "\033[?25h";
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
    buffer += "\033[H";

    std::istringstream ss(map_grid);
    std::string line;
    while (std::getline(ss, line))
    {
        buffer += "\033[K" + colorize(line) + "\n";
    }
    buffer += "\033[K\n";

    size_t start = chat_history.size() > 10 ? chat_history.size() - 10 : 0;
    for (size_t i = start; i < chat_history.size(); ++i)
    {
        buffer += "\033[K" + colorize(chat_history[i]) + "\n";
    }

    if (game_mode)
    {
        buffer += "\033[K\033[32m[WASD: Move, Space: Up, Shift: Down, T: Chat]\033[0m ";
    }
    else
    {
        buffer += "\033[K> " + input;
    }
    buffer += "\033[J";

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
} // namespace mcg::client