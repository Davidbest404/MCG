#pragma once

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <vector>

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
        return lerp(v, lerp(u, grad2(p_[A], x, y), grad2(p_[B], x - 1, y)),
                    lerp(u, grad2(p_[A + 1], x, y - 1), grad2(p_[B + 1], x - 1, y - 1)));
    }

    double noise3d(double x, double y, double z) const
    {
        int X = static_cast<int>(std::floor(x)) & 255;
        int Y = static_cast<int>(std::floor(y)) & 255;
        int Z = static_cast<int>(std::floor(z)) & 255;
        x -= std::floor(x);
        y -= std::floor(y);
        z -= std::floor(z);
        double u = fade(x);
        double v = fade(y);
        double w = fade(z);

        int A = p_[X] + Y, AA = p_[A] + Z, AB = p_[A + 1] + Z;
        int B = p_[X + 1] + Y, BA = p_[B] + Z, BB = p_[B + 1] + Z;

        return lerp(
            w,
            lerp(v, lerp(u, grad3(p_[AA], x, y, z), grad3(p_[BA], x - 1, y, z)),
                 lerp(u, grad3(p_[AB], x, y - 1, z), grad3(p_[BB], x - 1, y - 1, z))),
            lerp(v, lerp(u, grad3(p_[AA + 1], x, y, z - 1), grad3(p_[BA + 1], x - 1, y, z - 1)),
                 lerp(u, grad3(p_[AB + 1], x, y - 1, z - 1),
                      grad3(p_[BB + 1], x - 1, y - 1, z - 1))));
    }

private:
    std::vector<int> p_;

    static double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static double lerp(double t, double a, double b) { return a + t * (b - a); }

    static double grad2(int hash, double x, double y)
    {
        int h = hash & 7;
        double u = h < 4 ? x : y;
        double v = h < 4 ? y : x;
        return ((h & 1) ? -u : u) + ((h & 2) ? -2.0 * v : 2.0 * v);
    }

    static double grad3(int hash, double x, double y, double z)
    {
        int h = hash & 15;
        double u = h < 8 ? x : y;
        double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
        return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
    }
};

} // namespace mcg::server::noise