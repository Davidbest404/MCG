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

// Цвет солнца (высокие блоки светлеют к нему)
static const std::string SUN_COLOR = "FFF8DC";

// Освещение: знаковое (положительное = выше = светлее, отрицательное = ниже = темнее)
static std::string adjust_light(const std::string& hex, double factor)
{
    if (hex.length() != 6) return hex;
    int r = hex_to_int(hex, 0), g = hex_to_int(hex, 2), b = hex_to_int(hex, 4);

    if (factor >= 1.0)
    {
        double blend = std::min(factor - 1.0, 1.0);
        int tr = hex_to_int(SUN_COLOR, 0), tg = hex_to_int(SUN_COLOR, 2),
            tb = hex_to_int(SUN_COLOR, 4);
        r = r + static_cast<int>((tr - r) * blend);
        g = g + static_cast<int>((tg - g) * blend);
        b = b + static_cast<int>((tb - b) * blend);
    }
    else
    {
        r = static_cast<int>(r * factor);
        g = static_cast<int>(g * factor);
        b = static_cast<int>(b * factor);
    }
    return rgb_to_hex(r, g, b);
}

// Градиент неба по Z: глубоко под землёй = тёмно-фиолетовый, на поверхности = голубой, высоко =
// белый
static std::string sky_color(int z)
{
    if (z < -30) return "200040";
    if (z > 40) return "FFFFFF";

    if (z < 0)
    {
        double t = (z + 30) / 30.0;
        return rgb_to_hex(static_cast<int>(32 + t * (135 - 32)),
                          static_cast<int>(0 + t * (206 - 0)),
                          static_cast<int>(64 + t * (235 - 64)));
    }
    else
    {
        double t = z / 40.0;
        return rgb_to_hex(static_cast<int>(135 + t * (255 - 135)),
                          static_cast<int>(206 + t * (255 - 206)),
                          static_cast<int>(235 + t * (255 - 235)));
    }
}

std::string ClientRenderer::render_map(const nlohmann::json& data, float light_step)
{
    if (!data.contains("w") || !data.contains("h") || !data.contains("g"))
    {
        return "Error: Invalid map data\n";
    }

    int w = data["w"].get<int>();
    int h = data["h"].get<int>();
    std::string axis = data.value("a", "xy");
    std::string mode = data.value("m", "topdown");

    int px = 0, py = 0, pz = 0;
    if (data.contains("c") && data["c"].is_array() && data["c"].size() >= 3)
    {
        px = data["c"][0].get<int>();
        py = data["c"][1].get<int>();
        pz = data["c"][2].get<int>();
    }

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
                out += "[bg000000] [/bg]";
                continue;
            }

            const auto& cell = grid[idx];
            if (cell.is_null())
            {
                // Туман / небо
                int row_z = (axis == "xy") ? 0 : (pz + radius - v);
                out += "[bg" + sky_color(row_z) + "] [/bg]";
                continue;
            }

            std::string s = cell.value("s", " ");
            std::string f = cell.value("f", "FFFFFF");
            std::string b = cell.value("b", "000000");
            int d = cell.value("d", 0);  // Знаковое: z - pz
            int r = cell.value("r", 0);  // Расстояние луча (для тумана)
            int z = cell.value("z", pz); // Абсолютный Z (для градиента неба)

            // 1. СНАЧАЛА туман — смешиваем с небом своего Z-уровня
            if (mode == "mixed")
            {
                double fog_t = std::min(static_cast<double>(r) / 15.0, 1.0);
                std::string sky = sky_color(z);
                f = lerp_color(f, sky, fog_t);
                b = lerp_color(b, sky, fog_t);
            }

            // 2. ПОТОМ свет — смешиваем с солнцем по высоте
            double factor = std::pow(static_cast<double>(light_step), static_cast<double>(d));
            f = adjust_light(f, factor);
            b = adjust_light(b, factor);

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