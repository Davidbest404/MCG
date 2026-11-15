#pragma once

#include "server/world/world.h"

namespace mcg::server::fov
{

// Проверяет видимость в 3D (по горизонтали и вертикали)
bool has_los_3d(const World& world, int x0, int y0, int z0, int x1, int y1, int z1);

// Старая 2D-функция (можно оставить для совместимости)
bool has_los(const World& world, int x0, int y0, int z0, int x1, int y1, int z1);

} // namespace mcg::server::fov