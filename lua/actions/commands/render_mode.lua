return {
    name = "render_mode",
    description = "Change render mode: topdown / level / mixed",
    args = "<topdown|level|mixed>",
    category = "admin",
    on_command = function(player, args)
        if #args < 1 then
            local current = api.get_attr(player, "render_mode") or "topdown"
            api.broadcast("[cE]Current render mode: " .. current .. "[/c]")
            return
        end
        local mode = args[1]
        if mode == "topdown" or mode == "level" or mode == "mixed" then
            api.set_attr(player, "render_mode", mode)
            api.update_render(player) -- ÈÑÏÐÀÂËÅÍÎ
            api.broadcast("[cE]Render mode set to: " .. mode .. "[/c]")
        else
            api.broadcast("[cR]Invalid mode. Use: topdown / level / mixed[/c]")
        end
    end
}