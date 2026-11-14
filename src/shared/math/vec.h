#pragma once

#include <cstdint>

namespace mcg::math
{

template <typename T> struct Vec2
{
    T x{};
    T y{};

    constexpr Vec2() = default;
    constexpr Vec2(T x_, T y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(T s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(T s) const { return {x / s, y / s}; }

    constexpr Vec2& operator+=(const Vec2& o)
    {
        x += o.x;
        y += o.y;
        return *this;
    }
    constexpr Vec2& operator-=(const Vec2& o)
    {
        x -= o.x;
        y -= o.y;
        return *this;
    }

    constexpr bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    constexpr bool operator!=(const Vec2& o) const { return !(*this == o); }
};

template <typename T> struct Vec3
{
    T x{};
    T y{};
    T z{};

    constexpr Vec3() = default;
    constexpr Vec3(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}
    constexpr explicit Vec3(const Vec2<T>& v2, T z_ = T{}) : x(v2.x), y(v2.y), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(T s) const { return {x * s, y * s, z * s}; }

    constexpr Vec3& operator+=(const Vec3& o)
    {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    constexpr Vec3& operator-=(const Vec3& o)
    {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        return *this;
    }

    constexpr bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    constexpr bool operator!=(const Vec3& o) const { return !(*this == o); }
};

using Vec2i = Vec2<int32_t>;
using Vec2f = Vec2<double>;
using Vec3i = Vec3<int32_t>;
using Vec3f = Vec3<double>;

} // namespace mcg::math