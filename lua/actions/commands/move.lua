return {
    name = "move",
    description = "Move player in a direction",
    args = "<north|south|east|west>",
    category = "movement",
    admin_only = false,
    on_command = function(player, args)
        if #args < 1 then
            api.broadcast("Usage: /move <north|south|east|west>")
            return
        end
        
        local dir = args[1]
        local x = api.get_attr(player, "pos_x")
        local y = api.get_attr(player, "pos_y")
        local z = api.get_attr(player, "pos_z")
        
        if dir == "north" then y = y - 1
        elseif dir == "south" then y = y + 1
        elseif dir == "east" then x = x + 1
        elseif dir == "west" then x = x - 1
        else
            api.broadcast("Unknown direction: " .. dir)
            return
        end
        
        api.set_attr(player, "pos_x", x)
        api.set_attr(player, "pos_y", y)
        api.set_attr(player, "pos_z", z)
        
        local name = api.get_attr(player, "name")
        api.broadcast(name .. " moved " .. dir .. " to (" .. x .. ", " .. y .. ", " .. z .. ")")
    end
}