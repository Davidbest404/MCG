return {
    name = "move",
    description = "Move player",
    args = "<direction>",
    category = "movement",
    on_command = function(player, args)
        if #args < 1 then return end
        local dir = args[1]
        
        local x = api.get_attr(player, "pos_x")
        local y = api.get_attr(player, "pos_y")
        local z = api.get_attr(player, "pos_z")
        local is_flying = api.get_attr(player, "is_flying") or false
        
        local dx, dy, dz = 0, 0, 0
        if dir == "north" then dy = -1
        elseif dir == "south" then dy = 1
        elseif dir == "east" then dx = 1
        elseif dir == "west" then dx = -1
        elseif dir == "up" then dz = 1
        elseif dir == "down" then dz = -1
        end
        
        local nx, ny, nz = x + dx, y + dy, z + dz
        
        -- Вертикальное движение (полет)
        if dz ~= 0 then
            if is_flying then
                api.set_attr(player, "pos_z", nz)
                api.update_fov(player)
            end
            return
        end
        
        if is_flying then
            if api.is_walkable(nx, ny, nz) then
                api.set_attr(player, "pos_x", nx)
                api.set_attr(player, "pos_y", ny)
                api.update_fov(player)
            end
            return
        end

        -- ГРАВИТАЦИЯ (Падаем на 1 блок вниз за ход, если под нами пусто)
        if api.is_walkable(x, y, z - 1) then
            api.set_attr(player, "pos_z", z - 1)
            api.update_fov(player)
            return -- Падение занимает ход, больше ничего не делаем
        end
        
        -- Движение пешком
        if api.is_walkable(nx, ny, z) then
            api.set_attr(player, "pos_x", nx)
            api.set_attr(player, "pos_y", ny)
            api.update_fov(player)
        elseif api.is_walkable(nx, ny, z + 1) and api.is_walkable(nx, ny, z + 2) then
            -- Забираемся на 1 блок вверх
            api.set_attr(player, "pos_x", nx)
            api.set_attr(player, "pos_y", ny)
            api.set_attr(player, "pos_z", z + 1)
            api.update_fov(player)
        else
            api.broadcast("Blocked by wall!")
        end
    end
}