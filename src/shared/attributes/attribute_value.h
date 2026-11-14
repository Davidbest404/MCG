#pragma once

#include <nlohmann/json.hpp>

namespace mcg
{

// Тип значения атрибута. Может быть числом, строкой, bool, null,
// объектом или массивом. Хранится как nlohmann::json для единообразия
// сериализации (сеть, сейвы, Lua-мост — всё один формат).
using AttributeValue = nlohmann::json;

} // namespace mcg