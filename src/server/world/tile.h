#pragma once
#include <string>

namespace mcg::server
{
struct Tile
{
    std::string symbol = ""; // "" = воздух, " " = фон, символы = объекты
    std::string color_hex = "000000"; // Цвет символа (или фона, если symbol == " ")
    std::string bg_color_hex = ""; // Свой фон (если пусто, берётся из террейна)
    bool walkable = true;
    bool transparent = true;
    bool swimmable = false;
    int damage_per_tick = 0;
    float move_speed_mult = 1.0f;
};
} // namespace mcg::server