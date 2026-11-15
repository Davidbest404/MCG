function on_generate_chunk(cx, cy, cz)
    local success, err = pcall(function()
        for x = 0, 15 do
            for y = 0, 15 do
                local gx = cx * 16 + x
                local gy = cy * 16 + y
                
                local terrain_height = get_height(gx, gy)
                local biome = get_biome(gx, gy)
                
                local is_spawn = (gx >= -3 and gx <= 3 and gy >= -3 and gy <= 3)
                if is_spawn then
                    terrain_height = 0
                end
                
                -- Цвета травы по биомам
                local grass_color = "3E753B"  -- ИСПРАВЛЕНО: тёмно-зелёный
                if biome == "desert" then
                    grass_color = "EDC9AF"
                elseif biome == "forest" then
                    grass_color = "2D5A2D"  -- Тёмнее для леса
                end
                
                if is_spawn then
                    grass_color = "3E753B"
                end
                
                for z = 0, 15 do
                    local gz = cz * 16 + z
                    
                    if gz > terrain_height then
                        api.set_terrain(gx, gy, gz, "", "000000", true, true)
                    elseif gz == terrain_height then
                        api.set_terrain(gx, gy, gz, " ", grass_color, true, true)
                    else
                        api.set_terrain(gx, gy, gz, " ", "8B4513", false, false)
                    end
                end
            end
        end
    end)
    
    if not success then
        print("LUA ERROR in generator: " .. tostring(err))
    end
end