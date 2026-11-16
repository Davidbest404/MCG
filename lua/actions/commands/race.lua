return {
    name = "race",
    description = "Set your race",
    args = "<race_name|list>",
    category = "info",
    on_command = function(player, args)
        if #args < 1 or args[1] == "list" then
            local list = api.list_races()
            api.broadcast("[cE]Available races: " .. table.concat(list, ", ") .. "[/c]")
            return
        end
        local ok = api.set_race(player, args[1])
        if ok then
            api.update_render(player)
            api.broadcast("[cE]Race set to " .. args[1] .. "[/c]")
        else
            api.broadcast("[cR]Unknown race: " .. args[1] .. "[/c]")
        end
    end
}