// fov.h
#pragma once
#include "server/world/world.h"
namespace mcg::server::fov
{
bool has_los(const World& world, int x0, int y0, int z0, int x1, int y1, int z1);
}