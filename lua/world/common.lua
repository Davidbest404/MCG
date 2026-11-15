-- lua/world/common.lua
-- Общие функции для генерации мира и биомов

function get_height(gx, gy)
    local total = 0
    local amp = 1
    local freq = 0.01
    for i = 1, 4 do
        total = total + api.noise2d(gx * freq, gy * freq) * amp
        amp = amp * 0.5
        freq = freq * 2
    end
    return math.floor(total * 10.0)
end

function get_biome(gx, gy)
    local biome_noise = api.noise2d(gx * 0.001 + 10000, gy * 0.001 + 10000)
    local temp_noise = api.noise2d(gx * 0.002 + 20000, gy * 0.002 + 20000)
    
    if temp_noise > 0.3 and biome_noise < 0.4 then
        return "desert"
    elseif biome_noise > 0.6 then
        return "forest"
    else
        return "plains"
    end
end