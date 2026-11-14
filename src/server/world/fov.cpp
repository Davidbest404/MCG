// fov.cpp
#include "server/world/fov.h"
#include <cstdlib>
namespace mcg::server::fov
{
bool has_los(const World& world, int x0, int y0, int z0, int x1, int y1, int z1)
{
    if (z0 != z1) return false; // FOV только на одном этаже
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (true)
    {
        if (x0 == x1 && y0 == y1) return true;
        e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
        if (x0 == x1 && y0 == y1) return true;
        if (!world.is_walkable(x0, y0, z0)) return false;
    }
}
} // namespace mcg::server::fov