function use_firestorm(player_id, args)
    local radius = 4
    local damage = 80
    local x = get_x(player_id)
    local y = get_y(player_id)
    local targets = get_players_in_radius(x, y, radius)
    local killed = 0
    for i, target_id in ipairs(targets) do
        if target_id ~= player_id then
            local hp = get_hp(target_id)
            local new_hp = math.max(0, hp - damage)
            set_hp(target_id, new_hp)
            if new_hp == 0 then killed = killed + 1 end
        end
    end
    broadcast("🔥 Firestorm strikes! " .. killed .. " player(s) killed by " .. get_player_name(player_id))
    return "You cast Firestorm, dealing " .. damage .. " damage to all around you!"
end