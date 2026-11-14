#pragma once

#include "shared/math/vec.h"

#include <cmath>
#include <cstdint>

namespace mcg::math
{

// Режим координат выбирается на весь мир (в server.json).
enum class CoordinateMode
{
    Integer, // целые координаты, манхэттенское расстояние, без диагоналей
    Rational, // fixed-point ×100, евклидово расстояние, диагонали разрешены
};

// Координаты в rational-режиме хранятся как int32_t, умноженные на этот
// коэффициент. 100 = 2 знака после запятой.
constexpr int32_t kRationalScale = 100;

// ---- Манхэттенское расстояние (integer-режим) ----

inline int32_t manhattan_2d(const Vec3i& a, const Vec3i& b)
{
    return static_cast<int32_t>(std::abs(a.x - b.x) + std::abs(a.y - b.y));
}

inline int32_t manhattan_3d(const Vec3i& a, const Vec3i& b)
{
    return static_cast<int32_t>(std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z));
}

inline int32_t manhattan_2d(const Vec2i& a, const Vec2i& b)
{
    return static_cast<int32_t>(std::abs(a.x - b.x) + std::abs(a.y - b.y));
}

// ---- Евклидово расстояние (rational-режим) ----

// Возвращает "сырое" расстояние в fixed-point единицах (×100).
inline double euclidean_fixed(const Vec3i& a, const Vec3i& b)
{
    const double dx = static_cast<double>(a.x - b.x);
    const double dy = static_cast<double>(a.y - b.y);
    const double dz = static_cast<double>(a.z - b.z);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Возвращает расстояние в "реальных" единицах (делит на kRationalScale).
inline double euclidean(const Vec3i& a, const Vec3i& b)
{
    return euclidean_fixed(a, b) / static_cast<double>(kRationalScale);
}

// ---- Универсальная функция ----

inline double distance(CoordinateMode mode, const Vec3i& a, const Vec3i& b)
{
    if (mode == CoordinateMode::Integer)
    {
        return static_cast<double>(manhattan_3d(a, b));
    }
    return euclidean(a, b);
}

inline double distance(CoordinateMode mode, const Vec2i& a, const Vec2i& b)
{
    if (mode == CoordinateMode::Integer)
    {
        return static_cast<double>(manhattan_2d(a, b));
    }
    // Для 2D в rational-режиме используем 3D-вектор с z = 0.
    return euclidean(Vec3i{a.x, a.y, 0}, Vec3i{b.x, b.y, 0});
}

} // namespace mcg::math