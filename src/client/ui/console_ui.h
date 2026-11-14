#pragma once

#include <iostream>
#include <mutex>
#include <string>

namespace mcg::client
{

class ConsoleUI
{
public:
    ConsoleUI();
    ~ConsoleUI();

    // Печатает сообщение из другого потока, не ломая текущий ввод
    void print_message(const std::string& text);

    // Читает строку посимвольно, возвращает её по нажатию Enter
    std::string read_line(const std::string& prompt = "> ");

private:
    std::mutex io_mutex_;
    std::string current_input_;
    std::string prompt_;

#ifndef _WIN32
    struct termios* old_termios_ = nullptr;
#endif
};

} // namespace mcg::client