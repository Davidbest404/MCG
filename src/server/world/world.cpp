#include "server/world/world.h"
#include "shared/util/logging.h"
#include <cmath>
#include <exception>


namespace mcg::server
{

World::World(uint32_t seed, bool fog_enabled)
    : seed_(seed), fog_enabled_(fog_enabled), perlin_(seed)
{
    MCG_INFO("World initialized with seed: {} (Fog: {})", seed_, fog_enabled_);
}

uint64_t World::hash_chunk(int cx, int cy, int cz) const
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 40) |
           (static_cast<uint64_t>(static_cast<uint32_t>(cy)) << 20) |
           static_cast<uint64_t>(static_cast<uint32_t>(cz) & 0xFFFFF);
}

void World::ensure_chunk(int cx, int cy, int cz) const
{
    uint64_t hash = hash_chunk(cx, cy, cz);
    if (terrain_chunks_.find(hash) == terrain_chunks_.end())
    {
        auto& terrain = terrain_chunks_[hash];
        for (int x = 0; x < CHUNK_SIZE; ++x)
            for (int y = 0; y < CHUNK_SIZE; ++y)
                for (int z = 0; z < CHUNK_SIZE; ++z)
                    terrain.tiles[x][y][z] = {"", "000000", "", true, true, false, 0, 1.0f};

        auto& overlay = overlay_chunks_[hash];
        for (int x = 0; x < CHUNK_SIZE; ++x)
            for (int y = 0; y < CHUNK_SIZE; ++y)
                for (int z = 0; z < CHUNK_SIZE; ++z)
                {
                    overlay.present[x][y][z] = false;
                    overlay.overlays[x][y][z] = {"", "000000", "", true, true, false, 0, 1.0f};
                }

        // 1. Сначала генерируем базовый мир
        if (generator_cb_)
        {
            try
            {
                generator_cb_(cx, cy, cz);
            }
            catch (const std::exception& e)
            {
                MCG_ERROR("Exception in generator: {}", e.what());
            }
        }

        // 2. Потом добавляем украшения (деревья, цветы)
        if (decorator_cb_)
        {
            try
            {
                decorator_cb_(cx, cy, cz);
            }
            catch (const std::exception& e)
            {
                MCG_ERROR("Exception in decorator: {}", e.what());
            }
        }
    }
}

Tile World::get_terrain(int x, int y, int z) const
{
    int cx = x / CHUNK_SIZE, cy = y / CHUNK_SIZE, cz = z / CHUNK_SIZE;
    if (x < 0 && x % CHUNK_SIZE != 0) cx--;
    if (y < 0 && y % CHUNK_SIZE != 0) cy--;
    if (z < 0 && z % CHUNK_SIZE != 0) cz--;

    ensure_chunk(cx, cy, cz);

    const TerrainChunk& chunk = terrain_chunks_.at(hash_chunk(cx, cy, cz));
    return chunk.tiles[x - cx * CHUNK_SIZE][y - cy * CHUNK_SIZE][z - cz * CHUNK_SIZE];
}

bool World::has_overlay(int x, int y, int z) const
{
    int cx = x / CHUNK_SIZE, cy = y / CHUNK_SIZE, cz = z / CHUNK_SIZE;
    if (x < 0 && x % CHUNK_SIZE != 0) cx--;
    if (y < 0 && y % CHUNK_SIZE != 0) cy--;
    if (z < 0 && z % CHUNK_SIZE != 0) cz--;

    ensure_chunk(cx, cy, cz);

    const OverlayChunk& chunk = overlay_chunks_.at(hash_chunk(cx, cy, cz));
    return chunk.present[x - cx * CHUNK_SIZE][y - cy * CHUNK_SIZE][z - cz * CHUNK_SIZE];
}

Tile World::get_overlay(int x, int y, int z) const
{
    int cx = x / CHUNK_SIZE, cy = y / CHUNK_SIZE, cz = z / CHUNK_SIZE;
    if (x < 0 && x % CHUNK_SIZE != 0) cx--;
    if (y < 0 && y % CHUNK_SIZE != 0) cy--;
    if (z < 0 && z % CHUNK_SIZE != 0) cz--;

    ensure_chunk(cx, cy, cz);

    const OverlayChunk& chunk = overlay_chunks_.at(hash_chunk(cx, cy, cz));
    return chunk.overlays[x - cx * CHUNK_SIZE][y - cy * CHUNK_SIZE][z - cz * CHUNK_SIZE];
}

void World::set_terrain(int x, int y, int z, Tile t)
{
    int cx = x / CHUNK_SIZE, cy = y / CHUNK_SIZE, cz = z / CHUNK_SIZE;
    if (x < 0 && x % CHUNK_SIZE != 0) cx--;
    if (y < 0 && y % CHUNK_SIZE != 0) cy--;
    if (z < 0 && z % CHUNK_SIZE != 0) cz--;

    ensure_chunk(cx, cy, cz);

    TerrainChunk& chunk = terrain_chunks_.at(hash_chunk(cx, cy, cz));
    chunk.tiles[x - cx * CHUNK_SIZE][y - cy * CHUNK_SIZE][z - cz * CHUNK_SIZE] = t;
}

void World::set_overlay(int x, int y, int z, Tile t)
{
    int cx = x / CHUNK_SIZE, cy = y / CHUNK_SIZE, cz = z / CHUNK_SIZE;
    if (x < 0 && x % CHUNK_SIZE != 0) cx--;
    if (y < 0 && y % CHUNK_SIZE != 0) cy--;
    if (z < 0 && z % CHUNK_SIZE != 0) cz--;

    ensure_chunk(cx, cy, cz);

    OverlayChunk& chunk = overlay_chunks_.at(hash_chunk(cx, cy, cz));
    int lx = x - cx * CHUNK_SIZE;
    int ly = y - cy * CHUNK_SIZE;
    int lz = z - cz * CHUNK_SIZE;
    chunk.overlays[lx][ly][lz] = t;
    chunk.present[lx][ly][lz] = (t.symbol != "");
}

bool World::is_walkable(int x, int y, int z) const
{
    if (!get_terrain(x, y, z).walkable) return false;
    if (has_overlay(x, y, z)) return get_overlay(x, y, z).walkable;
    return true;
}

bool World::is_transparent(int x, int y, int z) const
{
    if (!get_terrain(x, y, z).transparent) return false;
    if (has_overlay(x, y, z)) return get_overlay(x, y, z).transparent;
    return true;
}

bool World::is_column_blocked(int x, int y, int z_min, int z_max) const
{
    for (int z = z_min; z <= z_max; ++z)
    {
        if (has_overlay(x, y, z) && !get_overlay(x, y, z).transparent)
        {
            return true;
        }
    }
    return false;
}

int World::get_surface_z(int x, int y) const
{
    for (int z = 20; z >= -20; --z)
    {
        if (!get_terrain(x, y, z).walkable) return z + 1;
        if (has_overlay(x, y, z) && !get_overlay(x, y, z).walkable) return z + 1;
    }
    return 0;
}

} // namespace mcg::server