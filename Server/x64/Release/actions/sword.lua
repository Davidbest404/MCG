function sword(player_id, args)
    -- цель не обязательна, но для совместимости с интерфейсом оставим
    local my_hp = get_hp(player_id)
    if my_hp <= 0 then
        send_to_player(player_id, "You are dead and cannot attack.")
        return
    end
    local my_x = get_x(player_id)
    local my_y = get_y(player_id)
    local players_in_range = get_players_in_radius(my_x, my_y, 2)
    local damage = 10
    local hit_count = 0
    for i, id in ipairs(players_in_range) do
        if id ~= player_id and get_hp(id) > 0 then
            local new_hp = math.max(0, get_hp(id) - damage)
            set_hp(id, new_hp)
            hit_count = hit_count + 1
            if new_hp <= 0 then
                broadcast(id .. " has been killed by " .. player_id .. "'s sword!")
            end
        end
    end
    if hit_count > 0 then
        broadcast(player_id .. " swings a sword, hitting " .. hit_count .. " opponent(s) for " .. damage .. " damage each!")
    else
        send_to_player(player_id, "No valid targets in range (radius 2).")
    end
end

function get_description()
    return "Sword attack, hits all enemies within 2 tiles (damage 10)."
end