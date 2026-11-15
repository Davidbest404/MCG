return function(cx, cy, cz)
    for x = 0, 15 do
        for y = 0, 15 do
            local gx = cx * 16 + x
            local gy = cy * 16 + y
            
            local terrain_height = get_height(gx, gy)
            local biome = get_biome(gx, gy)
            
            local is_spawn = (gx >= -3 and gx <= 3 and gy >= -3 and gy <= 3)
            if is_spawn then terrain_height = 0 end
            
            if not is_spawn and biome == "plains" and terrain_height < 5 and terrain_height > -1 then
                local flower_noise = api.noise2d(gx * 0.3 + 1000, gy * 0.3 + 1000)
                if flower_noise > 0.3 then
                    -- ÈÑÏĞÀÂËÅÍÎ: íà terrain_height, íå +1
                    api.set_overlay(gx, gy, terrain_height, "*", "FFAA00", true)
                end
            end
        end
    end
end