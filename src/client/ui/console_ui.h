#pragma once
#include <mutex>
#include <string>
#include <vector>

namespace mcg::client
{
class ConsoleUI
{
public:
    ConsoleUI();
    ~ConsoleUI();

    // Отрисовывает весь экран разом (вызывается только из главного потока)
    void draw(const std::string& map_grid, const std::vector<std::string>& chat_history,
              const std::string& input, bool game_mode);

    void flush_input_buffer();

    // Читает нажатие клавиши (не блокирующее)
    int read_key_non_blocking();

private:
#ifndef _WIN32
    struct termios* old_termios_ = nullptr;
#endif
};
} // namespace mcg::client