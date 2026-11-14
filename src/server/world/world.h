#pragma once

#include "server/world/tile.h"
#include <cstdint>
#include <unordered_map>

namespace mcg::server
{

class World
{
public:
    World(uint32_t seed = 0, bool fog_enabled = true);

    Tile get_tile(int x, int y, int z) const;
    void set_tile(int x, int y, int z, Tile t);
    bool is_walkable(int x, int y, int z) const;

    uint32_t seed() const { return seed_; }
    bool fog_enabled() const { return fog_enabled_; }

private:
    uint32_t seed_;
    bool fog_enabled_;
    std::unordered_map<uint64_t, Tile> tiles_;

    uint64_t hash_pos(int x, int y, int z) const;
};

} // namespace mcg::server