#include "server/world/world.h"
#include "shared/util/logging.h"
#include <cmath>

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

void World::generate_terrain_chunk(int cx, int cy, int cz) const
{
    TerrainChunk chunk;
    for (int x = 0; x < CHUNK_SIZE; ++x)
    {
        for (int y = 0; y < CHUNK_SIZE; ++y)
        {
            int gx = cx * CHUNK_SIZE + x;
            int gy = cy * CHUNK_SIZE + y;

            double h = perlin_.noise2d(gx * 0.05, gy * 0.05);
            int terrain_height = static_cast<int>(h * 8.0);

            if (gx >= -3 && gx <= 3 && gy >= -3 && gy <= 3)
            {
                terrain_height = 0;
            }

            for (int z = 0; z < CHUNK_SIZE; ++z)
            {
                int gz = cz * CHUNK_SIZE + z;
                if (gz > terrain_height)
                {
                    chunk.tiles[x][y][z] = {' ', true};
                }
                else if (gz == terrain_height)
                {
                    chunk.tiles[x][y][z] = {'#', false};
                }
                else
                {
                    double cave = perlin_.noise2d(gx * 0.1 + 100, gy * 0.1 + 100);
                    if (std::abs(cave) < 0.15 && gz < -2)
                    {
                        chunk.tiles[x][y][z] = {' ', true};
                    }
                    else
                    {
                        chunk.tiles[x][y][z] = {'#', false};
                    }
                }
            }
        }
    }
    terrain_chunks_[hash_chunk(cx, cy, cz)] = chunk;
}

void World::generate_overlay_chunk(int cx, int cy, int cz) const
{
    OverlayChunk chunk;
    for (int x = 0; x < CHUNK_SIZE; ++x)
        for (int y = 0; y < CHUNK_SIZE; ++y)
            for (int z = 0; z < CHUNK_SIZE; ++z)
            {
                chunk.present[x][y][z] = false;
                chunk.overlays[x][y][z] = {' ', true};
            }

    for (int x = 0; x < CHUNK_SIZE; ++x)
    {
        for (int y = 0; y < CHUNK_SIZE; ++y)
        {
            int gx = cx * CHUNK_SIZE + x;
            int gy = cy * CHUNK_SIZE + y;

            double h = perlin_.noise2d(gx * 0.05, gy * 0.05);
            int terrain_height = static_cast<int>(h * 8.0);
            if (gx >= -3 && gx <= 3 && gy >= -3 && gy <= 3) terrain_height = 0;

            int overlay_z = terrain_height + 1;

            for (int z = 0; z < CHUNK_SIZE; ++z)
            {
                int gz = cz * CHUNK_SIZE + z;
                if (gz == overlay_z)
                {
                    if (terrain_height <= -1)
                    {
                        chunk.overlays[x][y][z] = {'~', false}; // Вода
                        chunk.present[x][y][z] = true;
                    }
                    else if (terrain_height >= 6)
                    {
                        chunk.overlays[x][y][z] = {'*', true}; // Снег
                        chunk.present[x][y][z] = true;
                    }
                    else
                    {
                        chunk.overlays[x][y][z] = {'"', true}; // Трава
                        chunk.present[x][y][z] = true;
                    }
                }
            }
        }
    }
    overlay_chunks_[hash_chunk(cx, cy, cz)] = chunk;
}

Tile World::get_terrain(int x, int y, int z) const
{
    int cx = x / CHUNK_SIZE, cy = y / CHUNK_SIZE, cz = z / CHUNK_SIZE;
    if (x < 0 && x % CHUNK_SIZE != 0) cx--;
    if (y < 0 && y % CHUNK_SIZE != 0) cy--;
    if (z < 0 && z % CHUNK_SIZE != 0) cz--;

    uint64_t hash = hash_chunk(cx, cy, cz);
    if (terrain_chunks_.find(hash) == terrain_chunks_.end()) generate_terrain_chunk(cx, cy, cz);

    const TerrainChunk& chunk = terrain_chunks_.at(hash);
    return chunk.tiles[x - cx * CHUNK_SIZE][y - cy * CHUNK_SIZE][z - cz * CHUNK_SIZE];
}

bool World::has_overlay(int x, int y, int z) const
{
    int cx = x / CHUNK_SIZE, cy = y / CHUNK_SIZE, cz = z / CHUNK_SIZE;
    if (x < 0 && x % CHUNK_SIZE != 0) cx--;
    if (y < 0 && y % CHUNK_SIZE != 0) cy--;
    if (z < 0 && z % CHUNK_SIZE != 0) cz--;

    uint64_t hash = hash_chunk(cx, cy, cz);
    if (overlay_chunks_.find(hash) == overlay_chunks_.end()) generate_overlay_chunk(cx, cy, cz);

    const OverlayChunk& chunk = overlay_chunks_.at(hash);
    return chunk.present[x - cx * CHUNK_SIZE][y - cy * CHUNK_SIZE][z - cz * CHUNK_SIZE];
}

Tile World::get_overlay(int x, int y, int z) const
{
    int cx = x / CHUNK_SIZE, cy = y / CHUNK_SIZE, cz = z / CHUNK_SIZE;
    if (x < 0 && x % CHUNK_SIZE != 0) cx--;
    if (y < 0 && y % CHUNK_SIZE != 0) cy--;
    if (z < 0 && z % CHUNK_SIZE != 0) cz--;

    uint64_t hash = hash_chunk(cx, cy, cz);
    if (overlay_chunks_.find(hash) == overlay_chunks_.end()) generate_overlay_chunk(cx, cy, cz);

    const OverlayChunk& chunk = overlay_chunks_.at(hash);
    return chunk.overlays[x - cx * CHUNK_SIZE][y - cy * CHUNK_SIZE][z - cz * CHUNK_SIZE];
}

void World::set_overlay(int x, int y, int z, Tile t)
{
    int cx = x / CHUNK_SIZE, cy = y / CHUNK_SIZE, cz = z / CHUNK_SIZE;
    if (x < 0 && x % CHUNK_SIZE != 0) cx--;
    if (y < 0 && y % CHUNK_SIZE != 0) cy--;
    if (z < 0 && z % CHUNK_SIZE != 0) cz--;

    uint64_t hash = hash_chunk(cx, cy, cz);
    if (overlay_chunks_.find(hash) == overlay_chunks_.end()) generate_overlay_chunk(cx, cy, cz);

    OverlayChunk& chunk = overlay_chunks_.at(hash);
    int lx = x - cx * CHUNK_SIZE;
    int ly = y - cy * CHUNK_SIZE;
    int lz = z - cz * CHUNK_SIZE;
    chunk.overlays[lx][ly][lz] = t;
    chunk.present[lx][ly][lz] = (t.symbol != ' ');
}

bool World::is_walkable(int x, int y, int z) const
{
    if (!get_terrain(x, y, z).walkable) return false;
    if (has_overlay(x, y, z)) return get_overlay(x, y, z).walkable;
    return true;
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