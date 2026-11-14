#pragma once

#include "shared/attributes/attribute_value.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mcg
{

class AttributeMap
{
public:
    using MapType = std::unordered_map<std::string, AttributeValue>;

    // Возвращает значение или null (AttributeValue{}), если ключа нет.
    AttributeValue get(std::string_view key) const;

    void set(std::string_view key, AttributeValue value);

    bool has(std::string_view key) const;
    bool erase(std::string_view key);
    void clear();

    std::size_t size() const;
    bool empty() const;

    MapType::const_iterator begin() const { return map_.begin(); }
    MapType::const_iterator end() const { return map_.end(); }

    // Сериализация всей мапы в JSON-объект.
    nlohmann::json to_json() const;

    // Обратное преобразование. Если вход не объект — вернёт пустую мапу.
    static AttributeMap from_json(const nlohmann::json& j);

private:
    MapType map_;
};

} // namespace mcg