#pragma once

namespace mcg::server
{

struct Tile
{
    char symbol = '.';    // Символ для отрисовки
    bool walkable = true; // Можно ли пройти
};

} // namespace mcg::server