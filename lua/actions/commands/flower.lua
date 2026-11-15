return {
    name = "flower",
    description = "Spawns a flower at your position",
    args = "",
    category = "admin",
    on_command = function(player, args)
        local x = api.get_attr(player, "pos_x")
        local y = api.get_attr(player, "pos_y")
        local z = api.get_attr(player, "pos_z")
        
        -- Ставим цветок. Фон возьмётся автоматически из травы под ним
        api.set_tile(x + 1, y, z, "✿", "FF00FF")
        api.update_fov(player)
        api.broadcast("[cFF00FF]Цветок посажен![/c]")
    end
}