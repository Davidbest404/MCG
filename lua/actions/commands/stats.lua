return {
    name = "stats",
    description = "Show your current stats",
    args = "",
    category = "info",
    admin_only = false,
    on_command = function(player, args)
        local name = api.get_attr(player, "name")
        local hp = api.get_attr(player, "hp")
        local max_hp = api.get_attr(player, "max_hp")
        local x = api.get_attr(player, "pos_x")
        local y = api.get_attr(player, "pos_y")
        local z = api.get_attr(player, "pos_z")
        local uptime = player:uptime_seconds()
        
        local report = "=== Stats for " .. name .. " ===\n"
        report = report .. "HP: " .. hp .. "/" .. max_hp .. "\n"
        report = report .. "Position: (" .. x .. ", " .. y .. ", " .. z .. ")\n"
        report = report .. "Uptime: " .. uptime .. " seconds"
        
        api.broadcast(report)
    end
}