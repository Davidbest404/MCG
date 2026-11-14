#include "server/world/world.h"
#include "server/world/perlin_noise.h"
#include "shared/util/logging.h"
#include <cmath>

namespace mcg::server
{

World::World(uint32_t seed, bool fog_enabled) : seed_(seed), fog_enabled_(fog_enabled)
{
    MCG_INFO("Generating world with seed: {} (Fog: {})", seed_, fog_enabled_);

    noise::PerlinNoise perlin(seed_);

    // Генерируем область 100x100 на высотах z = 0 и z = 1 (для пещер)
    for (int x = -50; x <= 50; ++x)
    {
        for (int y = -50; y <= 50; ++y)
        {
            // Рельеф (холмы)
            double h = perlin.noise2d(x * 0.05, y * 0.05);

            if (h > 0.3)
            {
                // Горы
                tiles_[hash_pos(x, y, 0)] = {'^', false};
            }
            else if (h > 0.0)
            {
                // Лес / трава
                tiles_[hash_pos(x, y, 0)] = {'"', true};
            }
            else
            {
                // Вода / пустошь
                tiles_[hash_pos(x, y, 0)] = {'~', true};
            }

            // Пещеры (Z = -1)
            // Используем другой масштаб шума для пещер
            double cave = perlin.noise2d(x * 0.1 + 100, y * 0.1 + 100);
            if (std::abs(cave) > 0.4)
            {
                // Твердая порода
                tiles_[hash_pos(x, y, -1)] = {'#', false};
            }
            else
            {
                // Пустая пещера (можно ходить)
                tiles_[hash_pos(x, y, -1)] = {'.', true};
            }
        }
    }

    // Очищаем зону спавна
    for (int dx = -2; dx <= 2; ++dx)
        for (int dy = -2; dy <= 2; ++dy)
            for (int dz = -1; dz <= 0; ++dz)
                tiles_[hash_pos(dx, dy, dz)] = {'.', true};
}

uint64_t World::hash_pos(int x, int y, int z) const
{
    // Простой хеш для 3D координат
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 40) |
           (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 20) |
           static_cast<uint64_t>(static_cast<uint32_t>(z) & 0xFFFFF);
}

Tile World::get_tile(int x, int y, int z) const
{
    auto it = tiles_.find(hash_pos(x, y, z));
    if (it != tiles_.end()) return it->second;
    return {' ', true}; // Пустота в воздухе
}

void World::set_tile(int x, int y, int z, Tile t)
{
    tiles_[hash_pos(x, y, z)] = t;
}

bool World::is_walkable(int x, int y, int z) const
{
    return get_tile(x, y, z).walkable;
}

} // namespace mcg::server