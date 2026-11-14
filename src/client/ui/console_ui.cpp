#include "client/ui/console_ui.h"

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace mcg::client
{

ConsoleUI::ConsoleUI()
{
#ifndef _WIN32
    old_termios_ = new termios();
    tcgetattr(STDIN_FILENO, old_termios_);
    termios newt = *old_termios_;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
#endif
}

ConsoleUI::~ConsoleUI()
{
#ifndef _WIN32
    if (old_termios_)
    {
        tcsetattr(STDIN_FILENO, TCSANOW, old_termios_);
        delete old_termios_;
    }
#endif
}

void ConsoleUI::print_message(const std::string& text)
{
    std::lock_guard<std::mutex> lock(io_mutex_);
    // Очищаем текущую строку (Возврат каретки + затирание до конца строки)
    std::cout << "\r\033[K";
    std::cout << text << "\n";
    // Восстанавливаем приглашение и то, что игрок уже напечатал
    std::cout << prompt_ << current_input_ << std::flush;
}

std::string ConsoleUI::read_line(const std::string& prompt)
{
    {
        std::lock_guard<std::mutex> lock(io_mutex_);
        prompt_ = prompt;
        current_input_.clear();
        std::cout << prompt_ << std::flush;
    }

    while (true)
    {
        int c = 0;
#ifdef _WIN32
        c = _getch();
        if (c == 0 || c == 224)
        {
            _getch();
            continue;
        } // Игнорируем стрелки
#else
        c = getchar();
#endif
        if (c == '\r' || c == '\n')
        {
            std::lock_guard<std::mutex> lock(io_mutex_);
            std::cout << "\n";
            std::string line = current_input_;
            current_input_.clear();
            return line;
        }
        else if (c == 8 || c == 127) // Backspace
        {
            std::lock_guard<std::mutex> lock(io_mutex_);
            if (!current_input_.empty())
            {
                current_input_.pop_back();
                std::cout << "\b \b" << std::flush;
            }
        }
        else if (c >= 32 && c <= 126) // Печатные символы ASCII
        {
            std::lock_guard<std::mutex> lock(io_mutex_);
            current_input_.push_back(static_cast<char>(c));
            std::cout << static_cast<char>(c) << std::flush;
        }
    }
}

} // namespace mcg::client