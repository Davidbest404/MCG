#pragma once

#include "shared/attributes/attribute_map.h"
#include <cstdint>
#include <string>

namespace mcg::server
{

class Player
{
public:
    Player(uint64_t id) : id_(id)
    {
        // Дефолтные атрибуты для теста
        attributes_.set("hp", 100);
        attributes_.set("name", "Player_" + std::to_string(id));
    }

    uint64_t id() const { return id_; }
    AttributeMap& attributes() { return attributes_; }

private:
    uint64_t id_;
    AttributeMap attributes_;
};

} // namespace mcg::server