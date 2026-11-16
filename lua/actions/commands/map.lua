return {
    name = "map",
    description = "Show current map view",
    args = "",
    category = "info",
    on_command = function(player, args)
        api.update_render(player)
    end
}