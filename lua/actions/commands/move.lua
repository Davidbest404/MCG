return {
    name = "move",
    description = "Move player (north/south/east/west/up/down)",
    args = "<direction>",
    category = "movement",
    admin_only = false,
    on_command = function(player, args)
        if #args < 1 then return end
        local dir = args[1]
        
        local x = api.get_attr(player, "pos_x")
        local y = api.get_attr(player, "pos_y")
        local z = api.get_attr(player, "pos_z")
        local dx, dy, dz = 0, 0, 0
        
        if dir == "north" then dy = -1
        elseif dir == "south" then dy = 1
        elseif dir == "east" then dx = 1
        elseif dir == "west" then dx = -1
        elseif dir == "up" then dz = 1
        elseif dir == "down" then dz = -1
        else return end
        
        local nx, ny, nz = x + dx, y + dy, z + dz
        
        if api.is_walkable(nx, ny, nz) then
            api.set_attr(player, "pos_x", nx)
            api.set_attr(player, "pos_y", ny)
            api.set_attr(player, "pos_z", nz)
            api.update_fov(player)
        else
            api.broadcast("Blocked!")
        end
    end
}