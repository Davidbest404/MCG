-- Лечит всех в радиусе (по умолчанию 3 клетки, лечение 20 HP)
function use_heal_aura(player_id, args)
    local radius = tonumber(args[1]) or 3
    local heal = tonumber(args[2]) or 20
    local x = get_x(player_id)
    local y = get_y(player_id)
    local targets = get_players_in_radius(x, y, radius)
    local healed = 0
    for i, target_id in ipairs(targets) do
        local hp = get_hp(target_id)
        local max_hp = get_max_hp(target_id)
        if hp < max_hp then
            local new_hp = math.min(max_hp, hp + heal)
            set_hp(target_id, new_hp)
            healed = healed + 1
        end
    end
    broadcast("✨ " .. get_player_name(player_id) .. " uses healing aura! " .. healed .. " player(s) healed.")
    return "You healed " .. healed .. " allies nearby!"
end