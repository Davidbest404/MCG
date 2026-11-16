return {
    name = "move",
    description = "Move player (relative to camera)",
    args = "<north|south|east|west|up|down>",
    category = "movement",
    on_command = function(player, args)
        if #args < 1 then return end
        local dir = args[1]
        
        local x = api.get_attr(player, "pos_x")
        local y = api.get_attr(player, "pos_y")
        local z = api.get_attr(player, "pos_z")
        local is_flying = api.get_attr(player, "is_flying") or false
        local view_axis = api.get_attr(player, "view_axis") or "xy"
        local view_dir = api.get_attr(player, "view_dir") or "t"
        
        local dx, dy, dz = 0, 0, 0
        
        -- ќпредел€ем направление движени€ относительно оси взгл€да
        if view_axis == "xy" then
            -- TopDown: как обычно
            if dir == "north" then dy = -1
            elseif dir == "south" then dy = 1
            elseif dir == "east" then dx = 1
            elseif dir == "west" then dx = -1
            elseif dir == "up" then dz = 1
            elseif dir == "down" then dz = -1
            end
        elseif view_axis == "xz" then
            -- Side (Terraria): X - горизонталь, Y - глубина, Z - вертикаль
            if dir == "north" then 
                dy = (view_dir == "t") and -1 or 1
            elseif dir == "south" then
                dy = (view_dir == "t") and 1 or -1
            elseif dir == "east" then
                dx = (view_dir == "t") and 1 or -1
            elseif dir == "west" then
                dx = (view_dir == "t") and -1 or 1
            elseif dir == "up" then dz = 1
            elseif dir == "down" then dz = -1
            end
        elseif view_axis == "yz" then
            -- Side (Front): Y - горизонталь, X - глубина, Z - вертикаль
            if dir == "north" then
                dx = (view_dir == "t") and -1 or 1
            elseif dir == "south" then
                dx = (view_dir == "t") and 1 or -1
            elseif dir == "east" then
                dy = (view_dir == "t") and 1 or -1
            elseif dir == "west" then
                dy = (view_dir == "t") and -1 or 1
            elseif dir == "up" then dz = 1
            elseif dir == "down" then dz = -1
            end
        else
            -- Fallback
            if dir == "north" then dy = -1
            elseif dir == "south" then dy = 1
            elseif dir == "east" then dx = 1
            elseif dir == "west" then dx = -1
            elseif dir == "up" then dz = 1
            elseif dir == "down" then dz = -1
            end
        end
        
        local nx, ny, nz = x + dx, y + dy, z + dz
        
        -- ¬ертикальное движение (только в полЄте)
        if dz ~= 0 then
            if is_flying then
                api.set_attr(player, "pos_x", nx)
                api.set_attr(player, "pos_y", ny)
                api.set_attr(player, "pos_z", nz)
                api.update_render(player)
            end
            return
        end

        if is_flying then
            if api.is_walkable(nx, ny, nz) then
                api.set_attr(player, "pos_x", nx)
                api.set_attr(player, "pos_y", ny)
                api.set_attr(player, "pos_z", nz)
                api.update_render(player)
            end
            return
        end

        -- √равитаци€
        if api.is_walkable(x, y, z - 1) then
            api.set_attr(player, "pos_z", z - 1)
            api.update_render(player)
            return 
        end
        
        -- √оризонтальное движение с step-up
        if api.is_walkable(nx, ny, z) then
            api.set_attr(player, "pos_x", nx)
            api.set_attr(player, "pos_y", ny)
            api.update_render(player)
        elseif api.is_walkable(nx, ny, z + 1) and api.is_walkable(nx, ny, z + 2) then
            api.set_attr(player, "pos_x", nx)
            api.set_attr(player, "pos_y", ny)
            api.set_attr(player, "pos_z", z + 1)
            api.update_render(player)
        end
    end
}