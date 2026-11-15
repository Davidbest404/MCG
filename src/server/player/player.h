#pragma once

#include "shared/attributes/attribute_map.h"
#include <chrono>
#include <cstdint>
#include <string>

namespace mcg::server
{

class Player
{
public:
    Player(uint64_t id) : id_(id), connect_time_(std::chrono::steady_clock::now())
    {
        attributes_.set("hp", 100);
        attributes_.set("max_hp", 100);
        attributes_.set("name", "Player_" + std::to_string(id));
        attributes_.set("role", "player");
        attributes_.set("pos_x", 0);
        attributes_.set("pos_y", 0);
        attributes_.set("pos_z", 0);
        attributes_.set("max_step_up", 1);
    }

    uint64_t id() const { return id_; }
    AttributeMap& attributes() { return attributes_; }

    // Время в секундах с момента подключения
    int64_t uptime_seconds() const
    {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - connect_time_).count();
    }

private:
    uint64_t id_;
    AttributeMap attributes_;
    std::chrono::steady_clock::time_point connect_time_;
};

} // namespace mcg::server