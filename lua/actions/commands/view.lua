return {
    name = "view",
    description = "Change camera view",
    args = "<t|b> <xy|xz|yz>",
    category = "info",
    on_command = function(player, args)
        if #args < 2 then
            api.broadcast("[cE]Usage: /view <t|b> <xy|xz|yz>[/c]")
            api.broadcast("[cE]  t = toward (forward), b = backward[/c]")
            api.broadcast("[cE]  xy = top-down, xz / yz = side view[/c]")
            return
        end
        
        local dir = args[1]
        local axis = args[2]
        
        if dir ~= "t" and dir ~= "b" then
            api.broadcast("[cR]Direction must be 't' or 'b'[/c]")
            return
        end
        
        if axis ~= "xy" and axis ~= "xz" and axis ~= "yz" then
            api.broadcast("[cR]Axis must be 'xy', 'xz', or 'yz'[/c]")
            return
        end
        
        api.set_attr(player, "view_dir", dir)
        api.set_attr(player, "view_axis", axis)
        
        -- Автоматически переключаем режим рендера
        local new_mode = (axis == "xy") and "topdown" or "mixed"
        api.set_attr(player, "render_mode", new_mode)
        
        api.update_render(player)
        api.broadcast("[cE]View: " .. dir .. " " .. axis .. " | Mode: " .. new_mode .. "[/c]")
    end
}