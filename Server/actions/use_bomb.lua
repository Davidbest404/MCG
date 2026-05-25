-- Использование бомбы: /use_bomb [радиус] [урон] [x] [y]
-- Если x,y не указаны, взрыв происходит на позиции игрока
function use_bomb(player_id, args)
    local radius = tonumber(args[1]) or 2
    local damage = tonumber(args[2]) or 50
    local x, y
    if args[3] and args[4] then
        x = tonumber(args[3])
        y = tonumber(args[4])
    else
        x = get_x(player_id)
        y = get_y(player_id)
    end
    
    local targets = get_players_in_radius(x, y, radius)
    if #targets == 0 then
        return "No players in blast radius"
    end
    
    local affected = 0
    for i, target_id in ipairs(targets) do
        local hp = get_hp(target_id)
        if hp > 0 then
            local new_hp = math.max(0, hp - damage)
            set_hp(target_id, new_hp)
            affected = affected + 1
        end
    end
    
    broadcast("💣 BOOM! " .. get_player_name(player_id) .. " detonates a bomb! " .. affected .. " player(s) damaged.")
    return "You explode the bomb! " .. affected .. " player(s) take " .. damage .. " damage."
end