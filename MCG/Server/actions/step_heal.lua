function heal_on_step(player_id, x, y, tile_id)
    local hp = get_hp(player_id)
    local maxhp = get_max_hp(player_id)
    if hp < maxhp then
        set_hp(player_id, hp + 5)
        send_to_player(player_id, "You feel a gentle energy healing you +5 HP.")
    end
end

function get_description()
    return "Heals 5 HP each turn you stand on this tile"
end