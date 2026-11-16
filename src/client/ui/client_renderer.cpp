#include "client/ui/client_renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mcg::client
{

static int hex_to_int(const std::string& h, int offset)
{
    if (h.length() < offset + 2) return 0;
    try
    {
        return std::stoi(h.substr(offset, 2), nullptr, 16);
    }
    catch (...)
    {
        return 0;
    }
}

static std::string rgb_to_hex(int r, int g, int b)
{
    r = std::max(0, std::min(255, r));
    g = std::max(0, std::min(255, g));
    b = std::max(0, std::min(255, b));
    char buf[8];
    snprintf(buf, sizeof(buf), "%02X%02X%02X", r, g, b);
    return std::string(buf);
}

static std::string lerp_color(const std::string& a, const std::string& b, double t)
{
    int ar = hex_to_int(a, 0), ag = hex_to_int(a, 2), ab = hex_to_int(a, 4);
    int br = hex_to_int(b, 0), bg = hex_to_int(b, 2), bb = hex_to_int(b, 4);
    return rgb_to_hex(static_cast<int>(ar * (1.0 - t) + br * t),
                      static_cast<int>(ag * (1.0 - t) + bg * t),
                      static_cast<int>(ab * (1.0 - t) + bb * t));
}

static std::string to_monochrome(const std::string& hex)
{
    if (hex.length() != 6) return hex;
    int r = hex_to_int(hex, 0), g = hex_to_int(hex, 2), b = hex_to_int(hex, 4);
    int gray = static_cast<int>(0.299 * r + 0.587 * g + 0.114 * b);
    return rgb_to_hex(gray, gray, gray);
}

static std::string adjust_light(const std::string& hex, double factor, const std::string& sun,
                                const std::string& dark, float sun_strength)
{
    if (hex.length() != 6) return hex;
    int r = hex_to_int(hex, 0), g = hex_to_int(hex, 2), b = hex_to_int(hex, 4);

    if (factor >= 1.0)
    {
        double t = std::min((factor - 1.0) * sun_strength, 0.95);
        int tr = hex_to_int(sun, 0), tg = hex_to_int(sun, 2), tb = hex_to_int(sun, 4);
        r = r + static_cast<int>((tr - r) * t);
        g = g + static_cast<int>((tg - g) * t);
        b = b + static_cast<int>((tb - b) * t);
    }
    else
    {
        double t = 1.0 - factor;
        int dr = hex_to_int(dark, 0), dg = hex_to_int(dark, 2), db = hex_to_int(dark, 4);
        r = r + static_cast<int>((dr - r) * t);
        g = g + static_cast<int>((dg - g) * t);
        b = b + static_cast<int>((db - b) * t);
    }
    return rgb_to_hex(r, g, b);
}

std::string ClientRenderer::render_map(const nlohmann::json& data, float light_step)
{
    if (!data.contains("w") || !data.contains("h") || !data.contains("g")) return "Error\n";

    int w = data["w"].get<int>();
    int h = data["h"].get<int>();
    std::string axis = data.value("a", "xy");
    std::string mode = data.value("m", "topdown");
    bool monochrome = data.value("monochrome", false);
    bool no_fog = data.value("no_fog", false);

    int px = 0, py = 0, pz = 0;
    if (data.contains("c") && data["c"].is_array() && data["c"].size() >= 3)
    {
        px = data["c"][0].get<int>();
        py = data["c"][1].get<int>();
        pz = data["c"][2].get<int>();
    }

    std::string sun_color = data.value("sun", "FFF8DC");
    std::string dark_color = data.value("dark", "000000");
    std::string sky_low = data.value("sky_low", "200040");
    std::string sky_mid = data.value("sky_mid", "87CEEB");
    std::string sky_high = data.value("sky_high", "FFFFFF");
    float sun_strength = data.value("sun_strength", 0.5f);

    auto get_sky_color = [&](int z)
    {
        if (z < 0)
        {
            double t = std::clamp((z + 30) / 30.0, 0.0, 1.0);
            return lerp_color(sky_low, sky_mid, t);
        }
        else
        {
            double t = std::clamp(z / 40.0, 0.0, 1.0);
            return lerp_color(sky_mid, sky_high, t);
        }
    };

    auto grid = data["g"].get<std::vector<nlohmann::json>>();
    int radius = w / 2;

    std::string out;
    out.reserve(w * h * 20);

    for (int v = 0; v < h; ++v)
    {
        for (int u = 0; u < w; ++u)
        {
            size_t idx = v * w + u;
            if (idx >= grid.size())
            {
                out += " ";
                continue;
            }

            const auto& cell = grid[idx];
            if (cell.is_null())
            {
                int row_z = (axis == "xy") ? pz : (pz + radius - v);
                std::string sky = get_sky_color(row_z);
                if (monochrome) sky = to_monochrome(sky);
                out += "[bg" + sky + "] [/bg]";
                continue;
            }

            std::string s = cell.value("s", " ");
            std::string f = cell.value("f", "FFFFFF");
            std::string b = cell.value("b", "000000");
            int d = cell.value("d", 0);
            int r = cell.value("r", 0);
            int z = cell.value("z", pz);
            bool is_player = cell.value("is_player", false);

            if (monochrome)
            {
                f = to_monochrome(f);
                b = to_monochrome(b);
            }

            if (is_player)
            {
                double factor_bg =
                    std::pow(static_cast<double>(light_step), static_cast<double>(d));
                std::string bg_lit =
                    adjust_light(b, factor_bg, sun_color, dark_color, sun_strength);
                if (monochrome) bg_lit = to_monochrome(bg_lit);
                out += "[c" + f + "][bg" + bg_lit + "]" + s + "[/bg][/c]";
                continue;
            }

            if (mode == "mixed" && !no_fog)
            {
                double fog_t = std::min(static_cast<double>(r) / 15.0, 1.0);
                std::string sky = get_sky_color(z);
                if (monochrome) sky = to_monochrome(sky);
                f = lerp_color(f, sky, fog_t);
                b = lerp_color(b, sky, fog_t);
            }

            double factor = std::pow(static_cast<double>(light_step), static_cast<double>(d));
            f = adjust_light(f, factor, sun_color, dark_color, sun_strength);
            b = adjust_light(b, factor, sun_color, dark_color, sun_strength);
            if (monochrome)
            {
                f = to_monochrome(f);
                b = to_monochrome(b);
            }

            if (s == " ")
            {
                out += "[bg" + b + "] [/bg]";
            }
            else
            {
                out += "[c" + f + "][bg" + b + "]" + s + "[/bg][/c]";
            }
        }
        out += "\n";
    }
    return out;
}
} // namespace mcg::client