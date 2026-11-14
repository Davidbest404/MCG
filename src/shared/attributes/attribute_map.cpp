#include "shared/attributes/attribute_map.h"

namespace mcg
{

AttributeValue AttributeMap::get(std::string_view key) const
{
    // unordered_map<string,...> не умеет искать по string_view напрямую
    // в C++20 (только через heterogeneous lookup, который требует
    // прозрачного хешера). Для простоты и надёжности конвертируем.
    auto it = map_.find(std::string(key));
    if (it == map_.end())
    {
        return AttributeValue{};
    }
    return it->second;
}

void AttributeMap::set(std::string_view key, AttributeValue value)
{
    map_[std::string(key)] = std::move(value);
}

bool AttributeMap::has(std::string_view key) const
{
    return map_.find(std::string(key)) != map_.end();
}

bool AttributeMap::erase(std::string_view key)
{
    return map_.erase(std::string(key)) > 0;
}

void AttributeMap::clear()
{
    map_.clear();
}

std::size_t AttributeMap::size() const
{
    return map_.size();
}

bool AttributeMap::empty() const
{
    return map_.empty();
}

nlohmann::json AttributeMap::to_json() const
{
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [k, v] : map_)
    {
        j[k] = v;
    }
    return j;
}

AttributeMap AttributeMap::from_json(const nlohmann::json& j)
{
    AttributeMap m;
    if (!j.is_object())
    {
        return m;
    }
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        m.map_[it.key()] = it.value();
    }
    return m;
}

} // namespace mcg