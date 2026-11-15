-- Биом: Снежные вершины. Настраиваемый порог и цвет.
local SNOW_HEIGHT = 10    -- выше этого — снег
local SNOW_COLOR = "F0F8FF" -- цвет снега (можно менять)

return function(cx, cy, cz)
    for x = 0, 15 do
        for y = 0, 15 do
            local gx = cx * 16 + x
            local gy = cy * 16 + y
            
            local terrain_height = get_height(gx, gy)
            
            local is_spawn = (gx >= -3 and gx <= 3 and gy >= -3 and gy <= 3)
            if is_spawn then terrain_height = 0 end
            
            -- Ставим снег только на высоких горах
            if terrain_height >= SNOW_HEIGHT then
                -- Заменяем траву на снег (символ " ", но другой цвет)
                api.set_terrain(gx, gy, terrain_height, " ", SNOW_COLOR, true, true)
            end
        end
    end
end