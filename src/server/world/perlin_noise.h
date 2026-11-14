#pragma once

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace mcg::server::noise
{

class PerlinNoise
{
public:
    explicit PerlinNoise(uint32_t seed)
    {
        p_.resize(256);
        std::iota(p_.begin(), p_.end(), 0);
        std::mt19937 rng(seed);
        std::shuffle(p_.begin(), p_.end(), rng);
        p_.insert(p_.end(), p_.begin(), p_.end());
    }

    double noise2d(double x, double y) const
    {
        int X = static_cast<int>(std::floor(x)) & 255;
        int Y = static_cast<int>(std::floor(y)) & 255;
        x -= std::floor(x);
        y -= std::floor(y);
        double u = fade(x);
        double v = fade(y);
        int A = p_[X] + Y, B = p_[X + 1] + Y;
        return lerp(v, lerp(u, grad(p_[A], x, y), grad(p_[B], x - 1, y)),
                    lerp(u, grad(p_[A + 1], x, y - 1), grad(p_[B + 1], x - 1, y - 1)));
    }

private:
    std::vector<int> p_;
    static double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static double lerp(double t, double a, double b) { return a + t * (b - a); }
    static double grad(int hash, double x, double y)
    {
        int h = hash & 7;
        double u = h < 4 ? x : y;
        double v = h < 4 ? y : x;
        return ((h & 1) ? -u : u) + ((h & 2) ? -2.0 * v : 2.0 * v);
    }
};

} // namespace mcg::server::noise