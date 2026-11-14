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
        
        print("Lua: move.lua called with dir=" .. tostring(dir) .. ", pos=" .. x .. "," .. y .. "," .. z) -- ÎÒËÀÄÊÀ

        local dx, dy, dz = 0, 0, 0
        if dir == "north" then dy = -1
        elseif dir == "south" then dy = 1
        elseif dir == "east" then dx = 1
        elseif dir == "west" then dx = -1
        elseif dir == "up" then dz = 1
        elseif dir == "down" then dz = -1
        else
            api.broadcast("Unknown direction: " .. tostring(dir))
            return
        end
        
        local nx, ny, nz = x + dx, y + dy, z + dz
        
        if dir == "up" or dir == "down" then
            if is_flying then
                api.set_attr(player, "pos_x", nx)
                api.set_attr(player, "pos_y", ny)
                api.set_attr(player, "pos_z", nz)
                api.update_fov(player)
            else
                api.broadcast("You are not flying! Use /fly to toggle.")
            end
            return
        end
        
        -- Ãðàâèòàöèÿ
        if not is_flying and api.is_walkable(x, y, z - 1) then
            local fz = z
            while fz > -100 and api.is_walkable(x, y, fz - 1) do
                fz = fz - 1
            end
            api.set_attr(player, "pos_z", fz)
            z = fz
        end
        
        if is_flying then
            if api.is_walkable(nx, ny, nz) then
                api.set_attr(player, "pos_x", nx)
                api.set_attr(player, "pos_y", ny)
                api.set_attr(player, "pos_z", nz)
                api.update_fov(player)
            else
                api.broadcast("Wall in the way!")
            end
            return
        end
        
        -- Äâèæåíèå
        if api.is_walkable(nx, ny, z) then
            local fz = z
            while fz > -100 and api.is_walkable(nx, ny, fz - 1) do
                fz = fz - 1
            end
            api.set_attr(player, "pos_x", nx)
            api.set_attr(player, "pos_y", ny)
            api.set_attr(player, "pos_z", fz)
            api.update_fov(player)
        elseif api.is_walkable(nx, ny, z + 1) and api.is_walkable(nx, ny, z + 2) then
            api.set_attr(player, "pos_x", nx)
            api.set_attr(player, "pos_y", ny)
            api.set_attr(player, "pos_z", z + 1)
            api.update_fov(player)
        else
            api.broadcast("Blocked by wall!")
        end
    end
}