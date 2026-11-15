return {
    name = "render_mode",
    description = "Change render mode: auto / topdown / level",
    args = "<auto|topdown|level>",
    category = "admin",
    on_command = function(player, args)
        if #args < 1 then
            local current = api.get_attr(player, "render_mode") or "auto"
            api.broadcast("[cE]Current render mode: " .. current .. "[/c]")
            api.broadcast("[cE]Usage: /render_mode <auto|topdown|level>[/c]")
            return
        end
        
        local mode = args[1]
        if mode == "auto" or mode == "topdown" or mode == "level" then
            api.set_attr(player, "render_mode", mode)
            api.update_fov(player)
            api.broadcast("[cE]Render mode set to: " .. mode .. "[/c]")
        else
            api.broadcast("[cR]Invalid mode. Use: auto / topdown / level[/c]")
        end
    end
}