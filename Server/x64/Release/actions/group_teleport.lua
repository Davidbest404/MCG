-- Телепортирует всех игроков в радиусе (кроме себя) на свою позицию
function group_teleport(player_id, args)
    local radius = tonumber(args[1]) or 5
    local x = get_x(player_id)
    local y = get_y(player_id)
    local targets = get_players_in_radius(x, y, radius)
    local count = 0
    for i, target_id in ipairs(targets) do
        if target_id ~= player_id then
            set_x(target_id, x)
            set_y(target_id, y)
            count = count + 1
        end
    end
    broadcast("🌀 " .. get_player_name(player_id) .. " teleports " .. count .. " player(s) to their location!")
    return "You teleported " .. count .. " players to you."
end