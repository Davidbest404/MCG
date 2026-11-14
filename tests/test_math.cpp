#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "shared/math/distance.h"
#include "shared/math/vec.h"

using namespace mcg::math;
using Catch::Approx;

TEST_CASE("Vec2 arithmetic", "[math][vec]")
{
    const Vec2i a{1, 2};
    const Vec2i b{3, 4};

    REQUIRE(a + b == Vec2i{4, 6});
    REQUIRE(b - a == Vec2i{2, 2});
    REQUIRE(a * 3 == Vec2i{3, 6});
    REQUIRE(a != b);
    REQUIRE(a == Vec2i{1, 2});
}

TEST_CASE("Vec3 arithmetic", "[math][vec]")
{
    const Vec3i a{1, 2, 3};
    const Vec3i b{4, 5, 6};

    REQUIRE(a + b == Vec3i{5, 7, 9});
    REQUIRE(b - a == Vec3i{3, 3, 3});

    Vec3i c = a;
    c += b;
    REQUIRE(c == Vec3i{5, 7, 9});
}

TEST_CASE("Manhattan distance", "[math][distance]")
{
    const Vec3i a{0, 0, 0};
    const Vec3i b{3, 4, 0};

    REQUIRE(manhattan_2d(a, b) == 7);
    REQUIRE(manhattan_3d(a, b) == 7);

    const Vec3i c{0, 0, 5};
    REQUIRE(manhattan_3d(a, c) == 5);
}

TEST_CASE("Euclidean distance (rational mode)", "[math][distance]")
{
    // В rational-режиме координаты ×100.
    // Точка (0,0,0) и точка (300, 400, 0) = реальные (3.00, 4.00, 0) = 5.00.
    const Vec3i a{0, 0, 0};
    const Vec3i b{300, 400, 0};

    REQUIRE(euclidean(a, b) == Approx(5.0));
}

TEST_CASE("Universal distance()", "[math][distance]")
{
    const Vec3i a{0, 0, 0};
    const Vec3i b{300, 400, 0};

    REQUIRE(distance(CoordinateMode::Integer, a, b) == Approx(700.0));
    REQUIRE(distance(CoordinateMode::Rational, a, b) == Approx(5.0));
}