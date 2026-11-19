-- Генерация ландшафта по чанкам
function generate_chunk(cx, cy, size, seed)
    math.randomseed(seed + cx * 10000 + cy)
    local chunk = {}
    
    for y = 1, size do
        chunk[y] = {}
        for x = 1, size do
            -- Глобальные координаты для бесшовного перехода
            local gx = cx * size + x
            local gy = cy * size + y
            
            local n = math.sin(gx * 0.1) + math.cos(gy * 0.1) + (math.random() * 0.2)
            
            if n > 1.1 then
                chunk[y][x] = 1 -- Вода
            elseif n < -1.1 then
                chunk[y][x] = 2 -- Камень
            else
                chunk[y][x] = 0 -- Трава
            end
        end
    end
    return chunk
end