return {
    name = "map",
    description = "Show current map view",
    args = "",
    category = "info",
    admin_only = false,
    on_command = function(player, args)
        api.update_fov(player)
    end
}