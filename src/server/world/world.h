#pragma once

#include "server/world/perlin_noise.h"
#include "server/world/tile.h"
#include <cstdint>
#include <unordered_map>

namespace mcg::server
{

constexpr int CHUNK_SIZE = 16;

struct TerrainChunk
{
    Tile tiles[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE];
};

struct OverlayChunk
{
    Tile overlays[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE];
    bool present[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE];
};

class World
{
public:
    World(uint32_t seed = 0, bool fog_enabled = true);

    Tile get_terrain(int x, int y, int z) const;
    Tile get_overlay(int x, int y, int z) const;
    bool has_overlay(int x, int y, int z) const;
    void set_overlay(int x, int y, int z, Tile t);

    bool is_walkable(int x, int y, int z) const;
    int get_surface_z(int x, int y) const;

    uint32_t seed() const { return seed_; }
    bool fog_enabled() const { return fog_enabled_; }

private:
    uint32_t seed_;
    bool fog_enabled_;
    mutable std::unordered_map<uint64_t, TerrainChunk> terrain_chunks_;
    mutable std::unordered_map<uint64_t, OverlayChunk> overlay_chunks_;
    mutable noise::PerlinNoise perlin_;

    uint64_t hash_chunk(int cx, int cy, int cz) const;
    void generate_terrain_chunk(int cx, int cy, int cz) const;
    void generate_overlay_chunk(int cx, int cy, int cz) const;
};

} // namespace mcg::server