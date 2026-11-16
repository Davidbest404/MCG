#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace mcg::client
{
class ClientRenderer
{
public:
    static std::string render_map(const nlohmann::json& world_render, float light_step);
};
} // namespace mcg::client